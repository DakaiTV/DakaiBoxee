#ifdef __APPLE__
/*
 *      Copyright (C) 2005-2009 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "CoreAudioRenderer.h"
#include "GUISettings.h"
#include "Settings.h"
#include "utils/Atomics.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

extern "C"
{
extern UInt64 AudioConvertHostTimeToNanos(UInt64 inHostTime); 
}

/////////////////////////////////////////////////////////////////////////////////
// CAtomicAllocator: Wrapper class for lf_heap.
////////////////////////////////////////////////////////////////////////////////
CAtomicAllocator::CAtomicAllocator(size_t blockSize) :
  m_BlockSize(blockSize)
{
  lf_heap_init(&m_Heap, blockSize);
}

CAtomicAllocator::~CAtomicAllocator()
{
  lf_heap_deinit(&m_Heap);
}

void* CAtomicAllocator::Alloc()
{
  return lf_heap_alloc(&m_Heap);
}

void CAtomicAllocator::Free(void* p)
{
  lf_heap_free(&m_Heap, p);  
}

size_t CAtomicAllocator::GetBlockSize() 
{
  return m_BlockSize;
}

//////////////////////////////////////////////////////////////////////////////////////
// CSliceQueue: Lock-free queue for audio_slices
//////////////////////////////////////////////////////////////////////////////////////
CSliceQueue::CSliceQueue(size_t sliceSize) :
  m_TotalBytes(0),
  m_pPartialSlice(NULL),
  m_RemainderSize(0)
{
  m_pAllocator = new CAtomicAllocator(sliceSize + offsetof(audio_slice, data));
  lf_queue_init(&m_Queue);
}

CSliceQueue::~CSliceQueue()
{
  Clear();
  lf_queue_deinit(&m_Queue);
  delete m_pAllocator;
}

// NOTE: The value of m_TotalBytes is only guaranteed to be accurate to within one slice,
//  but is sufficient. This means that it may at times be one slice too high or one slice
//  tool low, but will never be < 0.
void CSliceQueue::Push(audio_slice* pSlice) 
{
  if (pSlice)
  {
    lf_queue_enqueue(&m_Queue, pSlice);
    AtomicAdd((long*)&m_TotalBytes, (long)pSlice->header.data_len);
  }
}

audio_slice* CSliceQueue::Pop()
{
  audio_slice* pSlice = (audio_slice*)lf_queue_dequeue(&m_Queue);
  if (pSlice)
    AtomicSubtract((long*)&m_TotalBytes, (long)pSlice->header.data_len);
  return pSlice;
}

// *** NOTE: AddData and GetData are thread-safe for multiple writers and one reader
size_t CSliceQueue::AddData(void* pBuf, size_t bufLen)
{
  size_t bytesLeft = bufLen;
  unsigned char* pData = (unsigned char*)pBuf;
  if (pBuf && bufLen)
  {
    while (bytesLeft)
    {
      // TODO: find a way to ensure success...
      audio_slice* pSlice = (audio_slice*)m_pAllocator->Alloc(); // Allocation should never fail. The heap grows automatically.
      if (!pSlice)
      {
        CLog::Log(LOGDEBUG, "CSliceQueue::AddData: Failed to allocate new slice");
        return bufLen - bytesLeft;
      }
      pSlice->header.data_len = m_pAllocator->GetBlockSize() - offsetof(audio_slice, data);
      if (pSlice->header.data_len >= bytesLeft) // plenty of room. move it all in.
      {
        memcpy(pSlice->get_data(), pData, bytesLeft);
        pSlice->header.data_len = bytesLeft; // Adjust the reported size of the container
      }
      else
      {
        memcpy(pSlice->get_data(), pData, pSlice->header.data_len); // Copy all we can into this slice. More to come.
      }
      bytesLeft -= pSlice->header.data_len;
      pData += pSlice->header.data_len;
      Push(pSlice);
    }
  }
  return  bufLen - bytesLeft;
}

size_t CSliceQueue::GetData(void* pBuf, size_t bufLen)
{
  if (!pBuf || !bufLen)
    return 0;
  
  size_t remainder = 0;
  audio_slice* pNext = NULL;
  size_t bytesUsed = 0;
  
  // See if we can fill the request out of our partial slice (if there is one)
  if (m_RemainderSize >= bufLen)
  {
    memcpy(pBuf, m_pPartialSlice->get_data() + m_pPartialSlice->header.data_len - m_RemainderSize , bufLen);
    m_RemainderSize -= bufLen;
    bytesUsed = bufLen;
  }
  else // Pull what we can from the partial slice and get the rest from complete slices
  {
    // Take what we can from the partial slice (if there is one)
    if (m_RemainderSize)
    {
      memcpy(pBuf, m_pPartialSlice->get_data() + m_pPartialSlice->header.data_len - m_RemainderSize , m_RemainderSize);
      bytesUsed += m_RemainderSize;
      m_RemainderSize = 0;
    }
    
    // Pull slices from the fifo until we have enough data
    do // TODO: The efficiency of this loop can be improved (a lot I imagine)
    {
      pNext = Pop();
      if (!pNext)
        break;
      size_t nextLen = pNext->header.data_len;
      if (bytesUsed + nextLen > bufLen) // Check for a partial slice
        remainder = nextLen - (bufLen - bytesUsed);
      memcpy((BYTE*)pBuf + bytesUsed, pNext->get_data(), nextLen - remainder);
      bytesUsed += (nextLen - remainder); // Increment output size (remainder will be captured separately)
      if (!remainder)
        m_pAllocator->Free(pNext); // Free the copied slice
    } while (bytesUsed < bufLen);    
  }
  
  // Clean up the previous partial slice
  if (!m_RemainderSize && m_pPartialSlice)
  {
    m_pAllocator->Free(m_pPartialSlice);
    m_pPartialSlice = NULL;
  }
  
  // Save off the new partial slice (if there is one)
  if (remainder)
  {
    m_pPartialSlice = pNext;
    m_RemainderSize = remainder;
  }
  
  return bytesUsed;
}

size_t CSliceQueue::GetTotalBytes() 
{
  return m_TotalBytes + m_RemainderSize;
}

void CSliceQueue::Clear()
{
  while (audio_slice* pSlice = Pop())
    m_pAllocator->Free(pSlice);
  m_pAllocator->Free(m_pPartialSlice);
  m_pPartialSlice = NULL;
  m_RemainderSize = 0;
}

//***********************************************************************************************
// Performance Monitoring Helper Class
//***********************************************************************************************
CCoreAudioPerformance::CCoreAudioPerformance() :
  m_TotalBytesIn(0),
  m_TotalBytesOut(0),
  m_ExpectedBytesPerSec(0),
  m_ActualBytesPerSec(0),
  m_Flags(0),
  m_WatchdogEnable(false),
  m_WatchdogInterval(0),
  m_LastWatchdogCheck(0),
  m_LastWatchdogBytesIn(0),
  m_LastWatchdogBytesOut(0),
  m_WatchdogBitrateSensitivity(0.99f),
  m_WatchdogPreroll(0)
{
  
}

CCoreAudioPerformance::~CCoreAudioPerformance()
{
  
}

void CCoreAudioPerformance::Init(UInt32 expectedBytesPerSec, UInt32 watchdogInterval /*= 1000*/, UInt32 flags /*=0*/)
{
  m_ExpectedBytesPerSec = expectedBytesPerSec;
  m_WatchdogInterval = watchdogInterval;
  m_Flags = flags;
}

void CCoreAudioPerformance::ReportData(UInt32 bytesIn, UInt32 bytesOut)
{
  m_TotalBytesIn += bytesIn;
  m_TotalBytesOut += bytesOut;
  
  if (!m_WatchdogEnable)
    return;
  
  // Perform watchdog funtions
  UInt32 time = CTimeUtils::GetTimeMS();
  if (!m_LastWatchdogCheck)
    m_LastWatchdogCheck = time;
  UInt32 deltaTime = time - m_LastWatchdogCheck;
  m_ActualBytesPerSec = (m_TotalBytesOut - m_LastWatchdogBytesOut) / ((float)deltaTime/1000.0f);
  if (deltaTime > m_WatchdogInterval)
  {
//    if (m_TotalBytesOut > m_WatchdogPreroll) // Allow m_WatchdogPreroll bytes to go by unmonitored
//    {
//      // Check outgoing bitrate
//      if (m_ActualBytesPerSec < m_WatchdogBitrateSensitivity * m_ExpectedBytesPerSec)
//        CLog::Log(LOGDEBUG, "CCoreAudioPerformance: Outgoing bitrate is lagging. Target: %lu, Actual: %lu. deltaTime was %lu", m_ExpectedBytesPerSec, m_ActualBytesPerSec, deltaTime);
//    }
    m_LastWatchdogCheck = time;
    m_LastWatchdogBytesIn = m_TotalBytesIn;
    m_LastWatchdogBytesOut = m_TotalBytesOut;    
  }
}

void CCoreAudioPerformance::EnableWatchdog(bool enable)
{
  if (!m_WatchdogEnable && enable)
    Reset();
  m_WatchdogEnable = enable;
}

void CCoreAudioPerformance::SetPreroll(UInt32 bytes)
{
  m_WatchdogPreroll = bytes;
}

void CCoreAudioPerformance::SetPreroll(float seconds)
{
  SetPreroll((UInt32)(seconds * m_ExpectedBytesPerSec));
}

void CCoreAudioPerformance::Reset()
{
  m_TotalBytesIn = 0;
  m_TotalBytesOut = 0;
  m_ActualBytesPerSec = 0;
  m_LastWatchdogCheck = 0;
  m_LastWatchdogBytesIn = 0;
  m_LastWatchdogBytesOut = 0;  
}

//***********************************************************************************************
// Contruction/Destruction
//***********************************************************************************************
CCoreAudioRenderer::CCoreAudioRenderer() :
  m_Pause(false),
  m_Initialized(false),
  m_CurrentVolume(0),
  m_ChunkLen(0),
  m_pCache(NULL),
  m_MaxCacheLen(0),
  m_OutputBufferIndex(0),
  m_queueTimestamp(0),
  m_Passthrough(false),
  m_EnableVolumeControl(true),
  m_AvgBytesPerSec(0)
{
  m_RunoutEvent = CreateEvent(NULL, false, false, NULL);  
  m_bNeedRunoutSignal = true;
}

CCoreAudioRenderer::~CCoreAudioRenderer()
{
  CloseHandle(m_RunoutEvent);
  Deinitialize();
}

//***********************************************************************************************
// Initialization
//***********************************************************************************************

// Macro to use for sanity checks in each method. 'x' is the return value if not initialized
#define VERIFY_INIT(x) \
if (!m_Initialized) \
return x

bool CCoreAudioRenderer::Initialize(IAudioCallback* pCallback, int iChannels, enum PCMChannels* channelMap, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, const char* strAudioCodec, bool bIsMusic, bool bPassthrough, bool bTimed, AudioMediaFormat audioMediaFormat)
{  
  if (m_Initialized) // Have to clean house before we start again. TODO: Should we return failure instead?
    Deinitialize();
  
  // TODO: If debugging, output information about all devices/streams
  
  // Attempt to find the configured output device
  AudioDeviceID outputDevice = CCoreAudioHardware::FindAudioDevice(g_guiSettings.GetString("audiooutput.audiodevice"));
  if (!outputDevice) // Fall back to the default device if no match is found
  {
    CLog::Log(LOGWARNING, "CoreAudioRenderer::Initialize: Unable to locate configured device, falling-back to the system default.");
    outputDevice = CCoreAudioHardware::GetDefaultOutputDevice();
    if (!outputDevice) // Not a lot to be done with no device. TODO: Should we just grab the first existing device?
      return false;
  }
  
  m_bNeedRunoutSignal = true;

  // TODO: Determine if the device is in-use/locked by another process.
  
  // Attach our output object to the device
  m_AudioDevice.Open(outputDevice);

  // If this is a passthrough (AC3/DTS) stream, attempt to handle it natively
  if (bPassthrough)
    m_Passthrough = InitializeEncoded(outputDevice, uiSamplesPerSec);

  // If this is a PCM stream, or we failed to handle a passthrough stream natively, 
  // prepare the standard interleaved PCM interface
  if (!m_Passthrough)
  {
    // Create the Output AudioUnit Component
    if (!m_AudioUnit.Open(kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple))
      return false;

    // Hook the Ouput AudioUnit to the selected device
    if (!m_AudioUnit.SetCurrentDevice(outputDevice))
      return false;
    
    // If we are here and this is a passthrough stream, native handling failed.
    // Try to handle it as IEC61937 data over straight PCM (DD-Wav)
    bool configured = false;
    if (bPassthrough)
    {
      CLog::Log(LOGDEBUG, "CoreAudioRenderer::Initialize: No suitable AC3 output format found. Attempting DD-Wav.");
      configured = InitializePCMEncoded(uiSamplesPerSec);
    }
    else // Standard PCM data
      configured = InitializePCM(iChannels, uiSamplesPerSec, uiBitsPerSample);
    
    if (!configured) // No suitable output format was able to be configured
      return false;
    
    // Configure the maximum number of frames that the AudioUnit will ask to process at one time.
    // If this is not called, there is no guarantee that the callback will ever be called.
    UInt32 bufferFrames = m_AudioUnit.GetBufferFrameSize(); // Size of the output buffer, in Frames
    if (!m_AudioUnit.SetMaxFramesPerSlice(bufferFrames))
      return false;    
    
    m_ChunkLen = bufferFrames * m_BytesPerFrame;  // This is the minimum amount of data that we will accept from a client

    // Setup the callback function that the AudioUnit will use to request data	
    if (!m_AudioUnit.SetRenderProc(RenderCallback, this))
      return false;
    
    // Initialize the Output AudioUnit
    if (!m_AudioUnit.Initialize())
      return false;

    // Log some information about the stream
    AudioStreamBasicDescription inputDesc_end, outputDesc_end;
    CStdString formatString;
    m_AudioUnit.GetInputFormat(&inputDesc_end);
    m_AudioUnit.GetOutputFormat(&outputDesc_end);
    CLog::Log(LOGDEBUG, "CoreAudioRenderer::Initialize: Input Stream Format %s", StreamDescriptionToString(inputDesc_end, formatString));
    CLog::Log(LOGDEBUG, "CoreAudioRenderer::Initialize: Output Stream Format %s", StreamDescriptionToString(outputDesc_end, formatString));    
  }
  
  m_MaxCacheLen = m_AvgBytesPerSec;     // Set the max cache size to 1 second of data. TODO: Make this more intelligent
  m_Pause = true;                       // Suspend rendering. We will start once we have some data.
  m_pCache = new CSliceQueue(m_ChunkLen); // Initialize our incoming data cache
  m_Initialized = true;

  SetCurrentVolume(g_stSettings.m_nVolumeLevel);
  
  Resume();
  
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::Initialize: Renderer Configuration - Chunk Len: %u, Max Cache: %lu (%0.0fms).", m_ChunkLen, m_MaxCacheLen, 1000.0 *(float)m_MaxCacheLen/(float)m_AvgBytesPerSec);
  CLog::Log(LOGINFO, "CoreAudioRenderer::Initialize: Successfully configured audio output.");  
  return true;
}

bool CCoreAudioRenderer::Deinitialize()
{
  VERIFY_INIT(true); // Not really a failure if we weren't initialized

  // Stop rendering
  Stop();
  // Reset our state
  m_ChunkLen = 0;
  m_MaxCacheLen = 0;
  m_AvgBytesPerSec = 0;
  if (m_Passthrough)
    m_AudioDevice.RemoveIOProc();
  m_AudioUnit.Close();
  m_OutputStream.Close();
  m_AudioDevice.Close();
  delete m_pCache;
  m_pCache = NULL;
  m_Initialized = false;
  m_EnableVolumeControl = true;
  
  CLog::Log(LOGINFO, "CoreAudioRenderer::Deinitialize: Renderer has been shut down.");
  
  return true;
}

//***********************************************************************************************
// Transport control methods
//***********************************************************************************************
bool CCoreAudioRenderer::Pause()
{
  VERIFY_INIT(false);
  
  if (!m_Pause)
  {
    CLog::Log(LOGDEBUG, "CoreAudioRenderer::Pause: Pausing Playback.");
    if (m_Passthrough)
      m_AudioDevice.Stop();
    else
      m_AudioUnit.Stop();
    m_Pause = true;
  }
  return true;
}

bool CCoreAudioRenderer::Resume()
{
  VERIFY_INIT(false);

  if (m_Pause)
  {
    CLog::Log(LOGDEBUG, "CoreAudioRenderer::Resume: Resuming Playback.");
    if (m_Passthrough)
      m_AudioDevice.Start();
    else
     m_AudioUnit.Start();
    m_Pause = false;
  }
  return true;
}

bool CCoreAudioRenderer::Stop()
{
  VERIFY_INIT(false);
  if (m_Passthrough)
    m_AudioDevice.Stop();
  else
    m_AudioUnit.Stop();

  m_Pause = true;
  m_pCache->Clear();

  return true;
}

//***********************************************************************************************
// Volume control methods
//***********************************************************************************************
LONG CCoreAudioRenderer::GetCurrentVolume() const
{
  return m_CurrentVolume;
}

void CCoreAudioRenderer::Mute(bool bMute)
{
  if (bMute)
    SetCurrentVolume(0);
  else
    SetCurrentVolume(m_CurrentVolume);
}

bool CCoreAudioRenderer::SetCurrentVolume(LONG nVolume)
{  
  VERIFY_INIT(false);

  if (m_EnableVolumeControl) // Don't change actual volume for encoded streams
  {  
    // Convert milliBels to percent
    Float32 volPct = pow(10.0f, (float)nVolume/2000.0f);
    
    // Try to set the volume. If it fails there is not a lot to be done.
    if (!m_AudioUnit.SetCurrentVolume(volPct))
      return false;
  }
  m_CurrentVolume = nVolume; // Store the volume setpoint. We need this to check for 'mute'
  return true;
}

//***********************************************************************************************
// Data management methods
//***********************************************************************************************
unsigned int CCoreAudioRenderer::GetSpace()
{
  VERIFY_INIT(0);
  return m_MaxCacheLen - m_pCache->GetTotalBytes(); // This is just an estimate, since the driver is asynchonously pulling data.
}

unsigned int CCoreAudioRenderer::AddPackets(const void* data, DWORD len, double pts, double duration)
{  
  VERIFY_INIT(0);
  
  // Require at least one 'chunk'. This allows us at least some measure of control over efficiency
  if (len < m_ChunkLen || m_pCache->GetTotalBytes() >= m_MaxCacheLen)
    return 0;

  unsigned int cacheSpace = GetSpace();  
  if (len > cacheSpace)
    return 0; // Wait until we can accept all of it
  
  size_t bytesUsed = m_pCache->AddData((void*)data, len);
  m_bNeedRunoutSignal = true;
  
  Resume();  // We have some data. Attmept to resume playback
  
  return bytesUsed; // Number of bytes added to cache;
}

float CCoreAudioRenderer::GetDelay()
{
  VERIFY_INIT(0);
  // Calculate the duration of the data in the cache
  float delay = (float)m_pCache->GetTotalBytes()/(float)m_AvgBytesPerSec;
  // TODO: Obtain hardware/os latency for better accuracy
  return delay;
}

float CCoreAudioRenderer::GetCacheTime()
{
  return GetDelay();
}

unsigned int CCoreAudioRenderer::GetChunkLen()
{
  return m_ChunkLen;
}

void CCoreAudioRenderer::WaitCompletion()
{
  VERIFY_INIT();

  if (m_pCache && m_pCache->GetTotalBytes() == 0) // The cache is already empty. There is nothing to wait for.
    return;
  
  UInt32 delay = (UInt32)(GetDelay() * 1000.0f);
  WaitForSingleObject(m_RunoutEvent, delay);
}

//***********************************************************************************************
// Rendering Methods
//***********************************************************************************************
OSStatus CCoreAudioRenderer::OnRender(AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{  
  if (!m_Initialized)
    CLog::Log(LOGERROR, "CCoreAudioRenderer::OnRender: Callback to de/unitialized renderer.");
  
  // Process the request
  UInt32 bytesRequested = ioData->mBuffers[m_OutputBufferIndex].mDataByteSize;
  
  memset(ioData->mBuffers[m_OutputBufferIndex].mData, 0, bytesRequested);
  
  UInt32 bytesRead = (UInt32)m_pCache->GetData(ioData->mBuffers[m_OutputBufferIndex].mData, bytesRequested);
  if (bytesRead < bytesRequested)
  {
    if (m_bNeedRunoutSignal)
    SetEvent(m_RunoutEvent); // Tell anyone who cares that the cache is empty
    m_bNeedRunoutSignal = false;
    ioData->mBuffers[m_OutputBufferIndex].mDataByteSize = bytesRead;
  }

  if (bytesRead > 0)
  {
    UInt64 hostMillis = AudioConvertHostTimeToNanos(inTimeStamp->mHostTime) / 1000000LL;
  UInt32 nTimeSent = (UInt32)(((float)bytesRead / (float)m_AvgBytesPerSec) * 1000.0);
  m_queueTimestamp = hostMillis + nTimeSent;
  }
  
  // Hard mute for formats that do not allow standard volume control. Throw away any actual data to keep the stream moving.
  if (!m_EnableVolumeControl && m_CurrentVolume <= VOLUME_MINIMUM) 
    ioData->mBuffers[m_OutputBufferIndex].mDataByteSize = 0;
  
  return noErr;
}

// Static Callback from AudioUnit 
OSStatus CCoreAudioRenderer::RenderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
  return ((CCoreAudioRenderer*)inRefCon)->OnRender(ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, ioData);
}

// Static Callback from AudioDevice
OSStatus CCoreAudioRenderer::DirectRenderCallback(AudioDeviceID inDevice, const AudioTimeStamp* inNow, const AudioBufferList* inInputData, const AudioTimeStamp* inInputTime, AudioBufferList* outOutputData, const AudioTimeStamp* inOutputTime, void* inClientData)
{
  CCoreAudioRenderer* pThis = (CCoreAudioRenderer*)inClientData;
  return pThis->OnRender(NULL, inInputTime, 0, outOutputData->mBuffers[0].mDataByteSize / pThis->m_BytesPerFrame, outOutputData);
}

//***********************************************************************************************
// Audio Device Initialization Methods
//***********************************************************************************************
bool CCoreAudioRenderer::InitializePCM(UInt32 channels, UInt32 samplesPerSecond, UInt32 bitsPerSample)
{  
  // Set the input stream format for the AudioUnit (this is what is being sent to us)
  AudioStreamBasicDescription inputFormat;
  inputFormat.mFormatID = kAudioFormatLinearPCM;			      //	Data encoding format
  inputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian
                           | kLinearPCMFormatFlagIsPacked   // Samples occupy all bits (not left or right aligned)
                           | kAudioFormatFlagIsSignedInteger;
  inputFormat.mChannelsPerFrame = channels;                 // Number of interleaved audiochannels
  inputFormat.mSampleRate = (Float64)samplesPerSecond;      //	the sample rate of the audio stream
  inputFormat.mBitsPerChannel = bitsPerSample;              // Number of bits per sample, per channel
  inputFormat.mBytesPerFrame = (bitsPerSample>>3) * channels; // Size of a frame == 1 sample per channel		
  inputFormat.mFramesPerPacket = 1;                         // The smallest amount of indivisible data. Always 1 for uncompressed audio 	
  inputFormat.mBytesPerPacket = inputFormat.mBytesPerFrame * inputFormat.mFramesPerPacket; 
  if (!m_AudioUnit.SetInputFormat(&inputFormat))
    return false;

  // TODO: Handle channel mapping
  
  m_BytesPerFrame = inputFormat.mBytesPerFrame;
  m_AvgBytesPerSec = inputFormat.mSampleRate * inputFormat.mBytesPerFrame;      // 1 sample per channel per frame
  m_EnableVolumeControl = true;
  
  return true;
}

bool CCoreAudioRenderer::InitializePCMEncoded(UInt32 sampleRate)
{
  m_AudioDevice.SetHogStatus(true); // Prevent any other application from using this device.
  m_AudioDevice.SetMixingSupport(false); // Try to disable mixing support. Effectiveness depends on the device.
  
  // Set the Sample Rate as defined by the spec.
  m_AudioDevice.SetNominalSampleRate((float)sampleRate);

  if (!InitializePCM(2, sampleRate, 16))
    return false;
  
  m_EnableVolumeControl = false; // Prevent attempts to change the output volume. It is not possible with encoded audio
  return true;  
}

bool CCoreAudioRenderer::InitializeEncoded(AudioDeviceID outputDevice, UInt32 sampleRate)
{
  //return false; // un-comment to force PCM Spoofing (DD-Wav). For testing use only.
  
  CStdString formatString;
  AudioStreamBasicDescription outputFormat;
  bzero(&outputFormat, sizeof(outputFormat));
  AudioStreamID outputStream = 0;
        
  // Fetch a list of the streams defined by the output device
  AudioStreamIdList streams;
  UInt32  streamIndex = 0;
  m_AudioDevice.GetStreams(&streams);
  
  while (!streams.empty())
  {
    // Get the next stream
    AudioStreamID id = streams.front();
    streams.pop_front(); // We copied it, now we are done with it
    
    CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Found %s stream - id: 0x%x, Terminal Type: 0x%x", 
              CCoreAudioStream::GetDirection(id) ? "Input" : "Output", 
              id, 
              CCoreAudioStream::GetTerminalType(id));

   // Probe physical formats
    StreamFormatList physicalFormats;
    CCoreAudioStream::GetAvailablePhysicalFormats(id, &physicalFormats);
    while (!physicalFormats.empty())
    {
      AudioStreamRangedDescription& desc = physicalFormats.front();
      CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded:    Considering Physical Format: %s", StreamDescriptionToString(desc.mFormat, formatString));
      if (desc.mFormat.mFormatID == kAudioFormat60958AC3 && desc.mFormat.mSampleRate == sampleRate)
      {
        outputFormat = desc.mFormat; // Select this format
        m_OutputBufferIndex = streamIndex; // TODO: Is this technically correct? Will each stream have it's own IOProc buffer?
        outputStream = id;
        break;
      }
      physicalFormats.pop_front();
    }    
    
    // TODO: How do we determine if this is the right stream (not just the right format) to use?
    if (outputFormat.mFormatID)
      break; // We found a suitable format. No need to continue.
    streamIndex++;
  }
  
  if (!outputFormat.mFormatID) // No match found
  {
    CLog::Log(LOGERROR, "CoreAudioRenderer::InitializeEncoded: Unable to identify suitable output format.");
    return false;
  }    

  m_ChunkLen = outputFormat.mBytesPerPacket; // 1 Chunk == 1 Packet
  m_AvgBytesPerSec = outputFormat.mChannelsPerFrame * (outputFormat.mBitsPerChannel>>3) * outputFormat.mSampleRate; // mBytesPerFrame is 0 for a cac3 stream
  m_BytesPerFrame = outputFormat.mChannelsPerFrame * (outputFormat.mBitsPerChannel>>3);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Selected stream[%u] - id: 0x%x, Physical Format: %s (%lu Bytes/sec.)", streamIndex, outputStream, StreamDescriptionToString(outputFormat, formatString), m_AvgBytesPerSec);  
  
  // TODO: Auto hogging sets this for us. Figure out how/when to turn it off or use it
  // It appears that leaving this set will aslo restore the previous stream format when the
  // Application exits. If auto hogging is set and we try to set hog mode, we will deadlock
  // From the SDK docs: "If the AudioDevice is in a non-mixable mode, the HAL will automatically take hog mode on behalf of the first process to start an IOProc."
  
  // Lock down the device.  This MUST be done PRIOR to switching to a non-mixable format, if it is done at all
  // If it is attempted after the format change, there is a high likelihood of a deadlock 
  // We may need to do this sooner to enable mix-disable (i.e. before setting the stream format)

  CCoreAudioHardware::SetAutoHogMode(false); // Auto-Hog does not always un-hog the device when changing back to a mixable mode. Handle this on our own until it is fixed.
  bool autoHog = CCoreAudioHardware::GetAutoHogMode();
  CLog::Log(LOGDEBUG, " CoreAudioRenderer::InitializeEncoded: Auto 'hog' mode is set to '%s'.", autoHog ? "On" : "Off");
  if (!autoHog) // Try to handle this ourselves
  {
    m_AudioDevice.SetHogStatus(true); // Hog the device if it is not set to be done automatically
    m_AudioDevice.SetMixingSupport(false); // Try to disable mixing. If we cannot, it may not be a problem
  }
  
  // Configure the output stream object
  m_OutputStream.Open(outputStream); // This is the one we will keep
  AudioStreamBasicDescription previousFormat;
  m_OutputStream.GetPhysicalFormat(outputStream, &previousFormat);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Previous Physical Format: %s (%lu Bytes/sec.)", StreamDescriptionToString(previousFormat, formatString), m_AvgBytesPerSec);
  m_OutputStream.SetPhysicalFormat(&outputFormat); // Set the active format (the old one will be reverted when we close)
  
  // Register for data request callbacks from the driver
  m_AudioDevice.AddIOProc(DirectRenderCallback, this);
  
  m_AudioDevice.Start();
  
  m_EnableVolumeControl = false; // Prevent attempts to change the output volume. It is not possible with encoded audio
  return true;
}

#endif


