#pragma once

#define MIDI_NAMESPACE midi
#define BEGIN_MIDI_NAMESPACE \
    namespace MIDI_NAMESPACE \
    {
#define END_MIDI_NAMESPACE }

#define USING_NAMESPACE_MIDI using namespace MIDI_NAMESPACE;

BEGIN_MIDI_NAMESPACE

template <class SerialPort, class _Settings>
class SerialMIDI
{
    typedef _Settings Settings;

public:
    SerialMIDI(SerialPort &inSerial)
        : mSerial(inSerial){};

public:
    static const bool thruActivated = true;

    void begin()
    {
        // Initialise the Serial port
        mSerial.begin(Settings::BaudRate);
    }

    bool beginTransmission(MidiType)
    {
        return true;
    };

    void write(byte value)
    {
        mSerial.write(value);
    };

    void endTransmission(){};

    byte read()
    {
        return mSerial.read();
    };

    unsigned available()
    {
        return mSerial.available();
    };

private:
    SerialPort &mSerial;
};

#define MIDI_CREATE_CUSTOM_INSTANCE(Type, SerialPort, Name, Settings)    \
    MIDI_NAMESPACE::SerialMIDI<Type, Settings> serial##Name(SerialPort); \
    MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<Type, Settings>> Name((MIDI_NAMESPACE::SerialMIDI<Type, Settings> &)serial##Name);

struct DefaultPlatform
{
    static unsigned long now() { return 0; };
};

template <class Transport, class _Settings, class _Platform = DefaultPlatform>
class MidiInterface
{
public:
    typedef _Settings Settings;
    typedef _Platform Platform;
    typedef Message<Settings::SysExMaxSize> MidiMessage;

public:
    inline MidiInterface(Transport &);
    inline ~MidiInterface();

public:
    void begin(Channel inChannel = 1);

public:
    inline bool read();

public:
    inline void setHandleNoteOff(NoteOffCallback fptr) { mNoteOffCallback = fptr; }
    inline void setHandleNoteOn(NoteOnCallback fptr) { mNoteOnCallback = fptr; }
    inline void setHandleControlChange(ControlChangeCallback fptr) { mControlChangeCallback = fptr; }
    inline void setHandleSystemExclusive(SystemExclusiveCallback fptr) { mSystemExclusiveCallback = fptr; }

private:
    void launchCallback();

    void (*mMessageCallback)(const MidiMessage &message) = nullptr;
    NoteOffCallback mNoteOffCallback = nullptr;
    NoteOnCallback mNoteOnCallback = nullptr;
    ControlChangeCallback mControlChangeCallback = nullptr;
    SystemExclusiveCallback mSystemExclusiveCallback = nullptr;

    // -------------------------------------------------------------------------
    // MIDI Soft Thru

public:
    inline Thru::Mode getFilterMode() const;
    inline bool getThruState() const;

    inline void turnThruOn(Thru::Mode inThruFilterMode = Thru::Full);
    inline void turnThruOff();
    inline void setThruFilterMode(Thru::Mode inThruFilterMode);

private:
    void thruFilter(byte inChannel);

    // -------------------------------------------------------------------------
    // MIDI Parsing

private:
    bool parse();
    inline void handleNullVelocityNoteOnAsNoteOff();
    inline bool inputFilter(Channel inChannel);
    inline void resetInput();
    inline void UpdateLastSentTime();

    // -------------------------------------------------------------------------
    // Transport

public:
    Transport *getTransport() { return &mTransport; };

private:
    Transport &mTransport;

    // -------------------------------------------------------------------------
    // Internal variables

private:
    Channel mInputChannel;
    StatusByte mRunningStatus_RX;
    StatusByte mRunningStatus_TX;
    byte mPendingMessage[3];
    unsigned mPendingMessageExpectedLength;
    unsigned mPendingMessageIndex;
    unsigned mCurrentRpnNumber;
    unsigned mCurrentNrpnNumber;
    bool mThruActivated : 1;
    Thru::Mode mThruFilterMode : 7;
    MidiMessage mMessage;
    unsigned long mLastMessageSentTime;
    unsigned long mLastMessageReceivedTime;
    unsigned long mSenderActiveSensingPeriodicity;
    bool mReceiverActiveSensingActivated;
    int8_t mLastError;

private:
    inline StatusByte getStatus(MidiType inType,
                                Channel inChannel) const;
};

// -----------------------------------------------------------------------------

unsigned encodeSysEx(const byte *inData,
                     byte *outSysEx,
                     unsigned inLength,
                     bool inFlipHeaderBits = false);
unsigned decodeSysEx(const byte *inSysEx,
                     byte *outData,
                     unsigned inLength,
                     bool inFlipHeaderBits = false);

END_MIDI_NAMESPACE