/*!
 *  @file       MIDI.hpp
 *  Project     Arduino MIDI Library
 *  @brief      MIDI Library for the Arduino - Inline implementations
 *  @author     Francois Best, lathoub
 *  @date       24/02/11
 *  @license    MIT - Copyright (c) 2015 Francois Best
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

BEGIN_MIDI_NAMESPACE

/// \brief Constructor for MidiInterface.
template <class Transport, class Settings, class Platform>
inline MidiInterface<Transport, Settings, Platform>::MidiInterface(Transport &inTransport)
    : mTransport(inTransport), mInputChannel(0), mRunningStatus_RX(InvalidType), mRunningStatus_TX(InvalidType), mPendingMessageExpectedLength(0), mPendingMessageIndex(0), mCurrentRpnNumber(0xffff), mCurrentNrpnNumber(0xffff), mThruActivated(true), mThruFilterMode(Thru::Full), mLastMessageSentTime(0), mLastMessageReceivedTime(0), mSenderActiveSensingPeriodicity(0), mReceiverActiveSensingActivated(false), mLastError(0)
{
    mSenderActiveSensingPeriodicity = Settings::SenderActiveSensingPeriodicity;
}

/*! \brief Destructor for MidiInterface.

 This is not really useful for the Arduino, as it is never called...
 */
template <class Transport, class Settings, class Platform>
inline MidiInterface<Transport, Settings, Platform>::~MidiInterface()
{
}

// -----------------------------------------------------------------------------

/*! \brief Call the begin method in the setup() function of the Arduino.

 All parameters are set to their default values:
 - Input channel set to 1 if no value is specified
 - Full thru mirroring
 */
template <class Transport, class Settings, class Platform>
void MidiInterface<Transport, Settings, Platform>::begin(Channel inChannel)
{
    // Initialise the Transport layer
    mTransport.begin();

    mInputChannel = inChannel;
    mRunningStatus_TX = InvalidType;
    mRunningStatus_RX = InvalidType;

    mPendingMessageIndex = 0;
    mPendingMessageExpectedLength = 0;

    mCurrentRpnNumber = 0xffff;
    mCurrentNrpnNumber = 0xffff;

    mLastMessageSentTime = Platform::now();

    mMessage.valid = false;
    mMessage.type = InvalidType;
    mMessage.channel = 0;
    mMessage.data1 = 0;
    mMessage.data2 = 0;
    mMessage.length = 0;

    mThruFilterMode = Thru::Full;
    mThruActivated = mTransport.thruActivated;
}

// -----------------------------------------------------------------------------
//                                  Input
// -----------------------------------------------------------------------------

/*! \addtogroup input
 @{
*/

/*! \brief Read messages from the serial port using the main input channel.

 \return True if a valid message has been stored in the structure, false if not.
 A valid message is a message that matches the input channel. \n\n
 If the Thru is enabled and the message matches the filter,
 it is sent back on the MIDI output.
 @see see setInputChannel()
 */
template <class Transport, class Settings, class Platform>
inline bool MidiInterface<Transport, Settings, Platform>::read()
{
    return read(mInputChannel);
}

/*! \brief Read messages on a specified channel.
 */
template <class Transport, class Settings, class Platform>
inline bool MidiInterface<Transport, Settings, Platform>::read(Channel inChannel)
{
#ifndef RegionActiveSending
    // Active Sensing. This message is intended to be sent
    // repeatedly to tell the receiver that a connection is alive. Use
    // of this message is optional. When initially received, the
    // receiver will expect to receive another Active Sensing
    // message each 300ms (max), and if it does not then it will
    // assume that the connection has been terminated. At
    // termination, the receiver will turn off all voices and return to
    // normal (non- active sensing) operation.
    if (Settings::UseSenderActiveSensing && (mSenderActiveSensingPeriodicity > 0) && (Platform::now() - mLastMessageSentTime) > mSenderActiveSensingPeriodicity)
    {
        sendActiveSensing();
        mLastMessageSentTime = Platform::now();
    }

    if (Settings::UseReceiverActiveSensing && mReceiverActiveSensingActivated && (mLastMessageReceivedTime + ActiveSensingTimeout < Platform::now()))
    {
        mReceiverActiveSensingActivated = false;

        mLastError |= 1UL << ErrorActiveSensingTimeout; // set the ErrorActiveSensingTimeout bit
        if (mErrorCallback)
            mErrorCallback(mLastError);
    }
#endif

    if (inChannel >= MIDI_CHANNEL_OFF)
        return false; // MIDI Input disabled.

    if (!parse())
        return false;

#ifndef RegionActiveSending

    if (Settings::UseReceiverActiveSensing && mMessage.type == ActiveSensing)
    {
        // When an ActiveSensing message is received, the time keeping is activated.
        // When a timeout occurs, an error message is send and time keeping ends.
        mReceiverActiveSensingActivated = true;

        // is ErrorActiveSensingTimeout bit in mLastError on
        if (mLastError & (1 << (ErrorActiveSensingTimeout - 1)))
        {
            mLastError &= ~(1UL << ErrorActiveSensingTimeout); // clear the ErrorActiveSensingTimeout bit
            if (mErrorCallback)
                mErrorCallback(mLastError);
        }
    }

    // Keep the time of the last received message, so we can check for the timeout
    if (Settings::UseReceiverActiveSensing && mReceiverActiveSensingActivated)
        mLastMessageReceivedTime = Platform::now();

#endif

    handleNullVelocityNoteOnAsNoteOff();

    const bool channelMatch = inputFilter(inChannel);
    if (channelMatch)
        launchCallback();

    thruFilter(inChannel);

    return channelMatch;
}

// -----------------------------------------------------------------------------

// Private method: MIDI parser
template <class Transport, class Settings, class Platform>
bool MidiInterface<Transport, Settings, Platform>::parse()
{
    if (mTransport.available() == 0)
        return false; // No data available.

    // clear the ErrorParse bit
    mLastError &= ~(1UL << ErrorParse);

    // Parsing algorithm:
    // Get a byte from the serial buffer.
    // If there is no pending message to be recomposed, start a new one.
    //  - Find type and channel (if pertinent)
    //  - Look for other bytes in buffer, call parser recursively,
    //    until the message is assembled or the buffer is empty.
    // Else, add the extracted byte to the pending message, and check validity.
    // When the message is done, store it.

    const byte extracted = mTransport.read();

    // Ignore Undefined
    if (extracted == Undefined_FD)
        return (Settings::Use1ByteParsing) ? false : parse();

    if (mPendingMessageIndex == 0)
    {
        // Start a new pending message
        mPendingMessage[0] = extracted;

        // Check for running status first
        if (isChannelMessage(getTypeFromStatusByte(mRunningStatus_RX)))
        {
            // Only these types allow Running Status

            // If the status byte is not received, prepend it
            // to the pending message
            if (extracted < 0x80)
            {
                mPendingMessage[0] = mRunningStatus_RX;
                mPendingMessage[1] = extracted;
                mPendingMessageIndex = 1;
            }
            // Else: well, we received another status byte,
            // so the running status does not apply here.
            // It will be updated upon completion of this message.
        }

        const MidiType pendingType = getTypeFromStatusByte(mPendingMessage[0]);

        switch (pendingType)
        {
        // 1 byte messages
        case Start:
        case Continue:
        case Stop:
        case Clock:
        case Tick:
        case ActiveSensing:
        case SystemReset:
        case TuneRequest:
            // Handle the message type directly here.
            mMessage.type = pendingType;
            mMessage.channel = 0;
            mMessage.data1 = 0;
            mMessage.data2 = 0;
            mMessage.valid = true;

            // Do not reset all input attributes, Running Status must remain unchanged.
            // We still need to reset these
            mPendingMessageIndex = 0;
            mPendingMessageExpectedLength = 0;

            return true;
            break;

        // 2 bytes messages
        case ProgramChange:
        case AfterTouchChannel:
        case TimeCodeQuarterFrame:
        case SongSelect:
            mPendingMessageExpectedLength = 2;
            break;

        // 3 bytes messages
        case NoteOn:
        case NoteOff:
        case ControlChange:
        case PitchBend:
        case AfterTouchPoly:
        case SongPosition:
            mPendingMessageExpectedLength = 3;
            break;

        case SystemExclusiveStart:
        case SystemExclusiveEnd:
            // The message can be any length
            // between 3 and MidiMessage::sSysExMaxSize bytes
            mPendingMessageExpectedLength = MidiMessage::sSysExMaxSize;
            mRunningStatus_RX = InvalidType;
            mMessage.sysexArray[0] = pendingType;
            break;

        case InvalidType:
        default:
            // This is obviously wrong. Let's get the hell out'a here.
            mLastError |= 1UL << ErrorParse; // set the ErrorParse bit
            if (mErrorCallback)
                mErrorCallback(mLastError); // LCOV_EXCL_LINE

            resetInput();
            return false;
            break;
        }

        if (mPendingMessageIndex >= (mPendingMessageExpectedLength - 1))
        {
            // Reception complete
            mMessage.type = pendingType;
            mMessage.channel = getChannelFromStatusByte(mPendingMessage[0]);
            mMessage.data1 = mPendingMessage[1];
            mMessage.data2 = 0; // Completed new message has 1 data byte
            mMessage.length = 1;

            mPendingMessageIndex = 0;
            mPendingMessageExpectedLength = 0;
            mMessage.valid = true;

            return true;
        }
        else
        {
            // Waiting for more data
            mPendingMessageIndex++;
        }

        return (Settings::Use1ByteParsing) ? false : parse();
    }
    else
    {
        // First, test if this is a status byte
        if (extracted >= 0x80)
        {
            // Reception of status bytes in the middle of an uncompleted message
            // are allowed only for interleaved Real Time message or EOX
            switch (extracted)
            {
            case Clock:
            case Start:
            case Tick:
            case Continue:
            case Stop:
            case ActiveSensing:
            case SystemReset:

                // Here we will have to extract the one-byte message,
                // pass it to the structure for being read outside
                // the MIDI class, and recompose the message it was
                // interleaved into. Oh, and without killing the running status..
                // This is done by leaving the pending message as is,
                // it will be completed on next calls.

                mMessage.type = (MidiType)extracted;
                mMessage.data1 = 0;
                mMessage.data2 = 0;
                mMessage.channel = 0;
                mMessage.length = 1;
                mMessage.valid = true;

                return true;

                // Exclusive
            case SystemExclusiveStart:
            case SystemExclusiveEnd:
                if ((mMessage.sysexArray[0] == SystemExclusiveStart) || (mMessage.sysexArray[0] == SystemExclusiveEnd))
                {
                    // Store the last byte (EOX)
                    mMessage.sysexArray[mPendingMessageIndex++] = extracted;
                    mMessage.type = SystemExclusive;

                    // Get length
                    mMessage.data1 = mPendingMessageIndex & 0xff;     // LSB
                    mMessage.data2 = byte(mPendingMessageIndex >> 8); // MSB
                    mMessage.channel = 0;
                    mMessage.length = mPendingMessageIndex;
                    mMessage.valid = true;

                    resetInput();

                    return true;
                }
                else
                {
                    // Well well well.. error.
                    mLastError |= 1UL << ErrorParse; // set the error bits
                    if (mErrorCallback)
                        mErrorCallback(mLastError); // LCOV_EXCL_LINE

                    resetInput();
                    return false;
                }

            default:
                break; // LCOV_EXCL_LINE - Coverage blind spot
            }
        }

        // Add extracted data byte to pending message
        if ((mPendingMessage[0] == SystemExclusiveStart) || (mPendingMessage[0] == SystemExclusiveEnd))
            mMessage.sysexArray[mPendingMessageIndex] = extracted;
        else
            mPendingMessage[mPendingMessageIndex] = extracted;

        // Now we are going to check if we have reached the end of the message
        if (mPendingMessageIndex >= (mPendingMessageExpectedLength - 1))
        {
            // SysEx larger than the allocated buffer size,
            // Split SysEx like so:
            //   first:  0xF0 .... 0xF0
            //   midlle: 0xF7 .... 0xF0
            //   last:   0xF7 .... 0xF7
            if ((mPendingMessage[0] == SystemExclusiveStart) || (mPendingMessage[0] == SystemExclusiveEnd))
            {
                auto lastByte = mMessage.sysexArray[Settings::SysExMaxSize - 1];
                mMessage.sysexArray[Settings::SysExMaxSize - 1] = SystemExclusiveStart;
                mMessage.type = SystemExclusive;

                // Get length
                mMessage.data1 = Settings::SysExMaxSize & 0xff;     // LSB
                mMessage.data2 = byte(Settings::SysExMaxSize >> 8); // MSB
                mMessage.channel = 0;
                mMessage.length = Settings::SysExMaxSize;
                mMessage.valid = true;

                // No need to check against the inputChannel,
                // SysEx ignores input channel
                launchCallback();

                mMessage.sysexArray[0] = SystemExclusiveEnd;
                mMessage.sysexArray[1] = lastByte;

                mPendingMessageIndex = 2;

                return false;
            }

            mMessage.type = getTypeFromStatusByte(mPendingMessage[0]);

            if (isChannelMessage(mMessage.type))
                mMessage.channel = getChannelFromStatusByte(mPendingMessage[0]);
            else
                mMessage.channel = 0;

            mMessage.data1 = mPendingMessage[1];
            // Save data2 only if applicable
            mMessage.data2 = mPendingMessageExpectedLength == 3 ? mPendingMessage[2] : 0;

            // Reset local variables
            mPendingMessageIndex = 0;
            mPendingMessageExpectedLength = 0;

            mMessage.valid = true;

            // Activate running status (if enabled for the received type)
            switch (mMessage.type)
            {
            case NoteOff:
            case NoteOn:
            case AfterTouchPoly:
            case ControlChange:
            case ProgramChange:
            case AfterTouchChannel:
            case PitchBend:
                // Running status enabled: store it from received message
                mRunningStatus_RX = mPendingMessage[0];
                break;

            default:
                // No running status
                mRunningStatus_RX = InvalidType;
                break;
            }
            return true;
        }
        else
        {
            // Then update the index of the pending message.
            mPendingMessageIndex++;

            return (Settings::Use1ByteParsing) ? false : parse();
        }
    }
}

// Private method, see midi_Settings.h for documentation
template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::handleNullVelocityNoteOnAsNoteOff()
{
    if (Settings::HandleNullVelocityNoteOnAsNoteOff &&
        getType() == NoteOn && getData2() == 0)
    {
        mMessage.type = NoteOff;
    }
}

// Private method: check if the received message is on the listened channel
template <class Transport, class Settings, class Platform>
inline bool MidiInterface<Transport, Settings, Platform>::inputFilter(Channel inChannel)
{
    // This method handles recognition of channel
    // (to know if the message is destinated to the Arduino)

    // First, check if the received message is Channel
    if (mMessage.type >= NoteOff && mMessage.type <= PitchBend)
    {
        // Then we need to know if we listen to it
        if ((mMessage.channel == inChannel) ||
            (inChannel == MIDI_CHANNEL_OMNI))
        {
            return true;
        }
        else
        {
            // We don't listen to this channel
            return false;
        }
    }
    else
    {
        // System messages are always received
        return true;
    }
}

// Private method: reset input attributes
template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::resetInput()
{
    mPendingMessageIndex = 0;
    mPendingMessageExpectedLength = 0;
    mRunningStatus_RX = InvalidType;
}

// -----------------------------------------------------------------------------

/*! \brief Get the last received message's type

 Returns an enumerated type. @see MidiType
 */
template <class Transport, class Settings, class Platform>
inline MidiType MidiInterface<Transport, Settings, Platform>::getType() const
{
    return mMessage.type;
}

/*! \brief Get the channel of the message stored in the structure.

 \return Channel range is 1 to 16.
 For non-channel messages, this will return 0.
 */
template <class Transport, class Settings, class Platform>
inline Channel MidiInterface<Transport, Settings, Platform>::getChannel() const
{
    return mMessage.channel;
}

/*! \brief Get the first data byte of the last received message. */
template <class Transport, class Settings, class Platform>
inline DataByte MidiInterface<Transport, Settings, Platform>::getData1() const
{
    return mMessage.data1;
}

/*! \brief Get the second data byte of the last received message. */
template <class Transport, class Settings, class Platform>
inline DataByte MidiInterface<Transport, Settings, Platform>::getData2() const
{
    return mMessage.data2;
}

/*! \brief Get the System Exclusive byte array.

 @see getSysExArrayLength to get the array's length in bytes.
 */
template <class Transport, class Settings, class Platform>
inline const byte *MidiInterface<Transport, Settings, Platform>::getSysExArray() const
{
    return mMessage.sysexArray;
}

/*! \brief Get the length of the System Exclusive array.

 It is coded using data1 as LSB and data2 as MSB.
 \return The array's length, in bytes.
 */
template <class Transport, class Settings, class Platform>
inline unsigned MidiInterface<Transport, Settings, Platform>::getSysExArrayLength() const
{
    return mMessage.getSysExSize();
}

/*! \brief Check if a valid message is stored in the structure. */
template <class Transport, class Settings, class Platform>
inline bool MidiInterface<Transport, Settings, Platform>::check() const
{
    return mMessage.valid;
}

// -----------------------------------------------------------------------------

template <class Transport, class Settings, class Platform>
inline Channel MidiInterface<Transport, Settings, Platform>::getInputChannel() const
{
    return mInputChannel;
}

/*! \brief Set the value for the input MIDI channel
 \param inChannel the channel value. Valid values are 1 to 16, MIDI_CHANNEL_OMNI
 if you want to listen to all channels, and MIDI_CHANNEL_OFF to disable input.
 */
template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::setInputChannel(Channel inChannel)
{
    mInputChannel = inChannel;
}

// -----------------------------------------------------------------------------

/*! \brief Extract an enumerated MIDI type from a status byte.

 This is a utility static method, used internally,
 made public so you can handle MidiTypes more easily.
 */
template <class Transport, class Settings, class Platform>
MidiType MidiInterface<Transport, Settings, Platform>::getTypeFromStatusByte(byte inStatus)
{
    if ((inStatus < 0x80) ||
        (inStatus == Undefined_F4) ||
        (inStatus == Undefined_F5) ||
        (inStatus == Undefined_FD))
        return InvalidType; // Data bytes and undefined.

    if (inStatus < 0xf0)
        // Channel message, remove channel nibble.
        return MidiType(inStatus & 0xf0);

    return MidiType(inStatus);
}

/*! \brief Returns channel in the range 1-16
 */
template <class Transport, class Settings, class Platform>
inline Channel MidiInterface<Transport, Settings, Platform>::getChannelFromStatusByte(byte inStatus)
{
    return Channel((inStatus & 0x0f) + 1);
}

template <class Transport, class Settings, class Platform>
bool MidiInterface<Transport, Settings, Platform>::isChannelMessage(MidiType inType)
{
    return (inType == NoteOff ||
            inType == NoteOn ||
            inType == ControlChange ||
            inType == AfterTouchPoly ||
            inType == AfterTouchChannel ||
            inType == PitchBend ||
            inType == ProgramChange);
}

// -----------------------------------------------------------------------------

/*! \brief Detach an external function from the given type.

 Use this method to cancel the effects of setHandle********.
 \param inType        The type of message to unbind.
 When a message of this type is received, no function will be called.
 */
template <class Transport, class Settings, class Platform>
void MidiInterface<Transport, Settings, Platform>::disconnectCallbackFromType(MidiType inType)
{
    switch (inType)
    {
    case NoteOff:
        mNoteOffCallback = nullptr;
        break;
    case NoteOn:
        mNoteOnCallback = nullptr;
        break;
    case AfterTouchPoly:
        mAfterTouchPolyCallback = nullptr;
        break;
    case ControlChange:
        mControlChangeCallback = nullptr;
        break;
    case ProgramChange:
        mProgramChangeCallback = nullptr;
        break;
    case AfterTouchChannel:
        mAfterTouchChannelCallback = nullptr;
        break;
    case PitchBend:
        mPitchBendCallback = nullptr;
        break;
    case SystemExclusive:
        mSystemExclusiveCallback = nullptr;
        break;
    case TimeCodeQuarterFrame:
        mTimeCodeQuarterFrameCallback = nullptr;
        break;
    case SongPosition:
        mSongPositionCallback = nullptr;
        break;
    case SongSelect:
        mSongSelectCallback = nullptr;
        break;
    case TuneRequest:
        mTuneRequestCallback = nullptr;
        break;
    case Clock:
        mClockCallback = nullptr;
        break;
    case Start:
        mStartCallback = nullptr;
        break;
    case Tick:
        mTickCallback = nullptr;
        break;
    case Continue:
        mContinueCallback = nullptr;
        break;
    case Stop:
        mStopCallback = nullptr;
        break;
    case ActiveSensing:
        mActiveSensingCallback = nullptr;
        break;
    case SystemReset:
        mSystemResetCallback = nullptr;
        break;
    default:
        break;
    }
}

/*! @} */ // End of doc group MIDI Callbacks

// Private - launch callback function based on received type.
template <class Transport, class Settings, class Platform>
void MidiInterface<Transport, Settings, Platform>::launchCallback()
{
    if (mMessageCallback != 0)
        mMessageCallback(mMessage);

    // The order is mixed to allow frequent messages to trigger their callback faster.
    switch (mMessage.type)
    {
        // Notes
    case NoteOff:
        if (mNoteOffCallback != nullptr)
            mNoteOffCallback(mMessage.channel, mMessage.data1, mMessage.data2);
        break;
    case NoteOn:
        if (mNoteOnCallback != nullptr)
            mNoteOnCallback(mMessage.channel, mMessage.data1, mMessage.data2);
        break;

        // Real-time messages
    case Clock:
        if (mClockCallback != nullptr)
            mClockCallback();
        break;
    case Start:
        if (mStartCallback != nullptr)
            mStartCallback();
        break;
    case Tick:
        if (mTickCallback != nullptr)
            mTickCallback();
        break;
    case Continue:
        if (mContinueCallback != nullptr)
            mContinueCallback();
        break;
    case Stop:
        if (mStopCallback != nullptr)
            mStopCallback();
        break;
    case ActiveSensing:
        if (mActiveSensingCallback != nullptr)
            mActiveSensingCallback();
        break;

        // Continuous controllers
    case ControlChange:
        if (mControlChangeCallback != nullptr)
            mControlChangeCallback(mMessage.channel, mMessage.data1, mMessage.data2);
        break;
    case PitchBend:
        if (mPitchBendCallback != nullptr)
            mPitchBendCallback(mMessage.channel, (int)((mMessage.data1 & 0x7f) | ((mMessage.data2 & 0x7f) << 7)) + MIDI_PITCHBEND_MIN);
        break;
    case AfterTouchPoly:
        if (mAfterTouchPolyCallback != nullptr)
            mAfterTouchPolyCallback(mMessage.channel, mMessage.data1, mMessage.data2);
        break;
    case AfterTouchChannel:
        if (mAfterTouchChannelCallback != nullptr)
            mAfterTouchChannelCallback(mMessage.channel, mMessage.data1);
        break;

    case ProgramChange:
        if (mProgramChangeCallback != nullptr)
            mProgramChangeCallback(mMessage.channel, mMessage.data1);
        break;
    case SystemExclusive:
        if (mSystemExclusiveCallback != nullptr)
            mSystemExclusiveCallback(mMessage.sysexArray, mMessage.getSysExSize());
        break;

        // Occasional messages
    case TimeCodeQuarterFrame:
        if (mTimeCodeQuarterFrameCallback != nullptr)
            mTimeCodeQuarterFrameCallback(mMessage.data1);
        break;
    case SongPosition:
        if (mSongPositionCallback != nullptr)
            mSongPositionCallback(unsigned((mMessage.data1 & 0x7f) | ((mMessage.data2 & 0x7f) << 7)));
        break;
    case SongSelect:
        if (mSongSelectCallback != nullptr)
            mSongSelectCallback(mMessage.data1);
        break;
    case TuneRequest:
        if (mTuneRequestCallback != nullptr)
            mTuneRequestCallback();
        break;

    case SystemReset:
        if (mSystemResetCallback != nullptr)
            mSystemResetCallback();
        break;

    case InvalidType:
    default:
        break; // LCOV_EXCL_LINE - Unreacheable code, but prevents unhandled case warning.
    }
}

/*! @} */ // End of doc group MIDI Input

// -----------------------------------------------------------------------------
//                                  Thru
// -----------------------------------------------------------------------------

/*! \addtogroup thru
 @{
 */

/*! \brief Set the filter for thru mirroring
 \param inThruFilterMode a filter mode

 @see Thru::Mode
 */
template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::setThruFilterMode(Thru::Mode inThruFilterMode)
{
    mThruFilterMode = inThruFilterMode;
    mThruActivated = mThruFilterMode != Thru::Off;
}

template <class Transport, class Settings, class Platform>
inline Thru::Mode MidiInterface<Transport, Settings, Platform>::getFilterMode() const
{
    return mThruFilterMode;
}

template <class Transport, class Settings, class Platform>
inline bool MidiInterface<Transport, Settings, Platform>::getThruState() const
{
    return mThruActivated;
}

template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::turnThruOn(Thru::Mode inThruFilterMode)
{
    mThruActivated = true;
    mThruFilterMode = inThruFilterMode;
}

template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::turnThruOff()
{
    mThruActivated = false;
    mThruFilterMode = Thru::Off;
}

template <class Transport, class Settings, class Platform>
inline void MidiInterface<Transport, Settings, Platform>::UpdateLastSentTime()
{
    if (Settings::UseSenderActiveSensing && mSenderActiveSensingPeriodicity)
        mLastMessageSentTime = Platform::now();
}

/*! @} */ // End of doc group MIDI Thru

// This method is called upon reception of a message
// and takes care of Thru filtering and sending.
// - All system messages (System Exclusive, Common and Real Time) are passed
//   to output unless filter is set to Off.
// - Channel messages are passed to the output whether their channel
//   is matching the input channel and the filter setting
template <class Transport, class Settings, class Platform>
void MidiInterface<Transport, Settings, Platform>::thruFilter(Channel inChannel)
{
    // If the feature is disabled, don't do anything.
    if (!mThruActivated || (mThruFilterMode == Thru::Off))
        return;

    // First, check if the received message is Channel
    if (mMessage.type >= NoteOff && mMessage.type <= PitchBend)
    {
        const bool filter_condition = ((mMessage.channel == inChannel) ||
                                       (inChannel == MIDI_CHANNEL_OMNI));

        // Now let's pass it to the output
        switch (mThruFilterMode)
        {
        case Thru::Full:
            send(mMessage.type,
                 mMessage.data1,
                 mMessage.data2,
                 mMessage.channel);
            break;

        case Thru::SameChannel:
            if (filter_condition)
            {
                send(mMessage.type,
                     mMessage.data1,
                     mMessage.data2,
                     mMessage.channel);
            }
            break;

        case Thru::DifferentChannel:
            if (!filter_condition)
            {
                send(mMessage.type,
                     mMessage.data1,
                     mMessage.data2,
                     mMessage.channel);
            }
            break;

        default:
            break;
        }
    }
    else
    {
        // Send the message to the output
        switch (mMessage.type)
        {
            // Real Time and 1 byte
        case Clock:
        case Start:
        case Stop:
        case Continue:
        case ActiveSensing:
        case SystemReset:
        case TuneRequest:
            sendRealTime(mMessage.type);
            break;

        case SystemExclusive:
            // Send SysEx (0xf0 and 0xf7 are included in the buffer)
            sendSysEx(getSysExArrayLength(), getSysExArray(), true);
            break;

        case SongSelect:
            sendSongSelect(mMessage.data1);
            break;

        case SongPosition:
            sendSongPosition(mMessage.data1 | ((unsigned)mMessage.data2 << 7));
            break;

        case TimeCodeQuarterFrame:
            sendTimeCodeQuarterFrame(mMessage.data1, mMessage.data2);
            break;

        default:
            break; // LCOV_EXCL_LINE - Unreacheable code, but prevents unhandled case warning.
        }
    }
}

END_MIDI_NAMESPACE
