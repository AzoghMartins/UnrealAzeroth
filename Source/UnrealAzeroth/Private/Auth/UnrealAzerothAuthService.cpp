#include "Auth/UnrealAzerothAuthService.h"

#include "Algo/Reverse.h"
#include "Auth/UnrealAzerothSession.h"
#include "AddressInfoTypes.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Settings/UnrealAzerothSettings.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "UnrealAzerothLog.h"

#pragma push_macro("UI")
#define UI OSSL_UI
THIRD_PARTY_INCLUDES_START
#include <openssl/bn.h>
#include <openssl/rand.h>
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("UI")

namespace
{
constexpr int32 ConnectTimeoutMs = 10'000;
constexpr int32 IoTimeoutMs = 10'000;
constexpr int32 MaxWorldPacketSizeWithOpcode = 1024 * 1024;
constexpr uint16 WowClientBuild = 12340;

constexpr uint8 AuthLogonChallengeOpcode = 0x00;
constexpr uint8 AuthLogonProofOpcode = 0x01;
constexpr uint8 AuthRealmListOpcode = 0x10;

constexpr uint8 AuthSuccessCode = 0x00;

constexpr uint8 RealmFlagOffline = 0x02;
constexpr uint8 RealmFlagSpecifyBuild = 0x04;

constexpr uint16 CmsgCharEnum = 0x0037;
constexpr uint16 SmsgCharEnum = 0x003B;
constexpr uint16 SmsgAuthChallenge = 0x01EC;
constexpr uint16 CmsgAuthSession = 0x01ED;
constexpr uint16 SmsgAuthResponse = 0x01EE;

constexpr uint8 WorldAuthOk = 0x0C;

constexpr int32 LocalErrorInvalidConfiguration = 1001;
constexpr int32 LocalErrorNetworkFailure = 1002;
constexpr int32 LocalErrorProtocolFailure = 1003;
constexpr int32 LocalErrorUnsupportedSecurity = 1004;
constexpr int32 LocalErrorInvalidSession = 1005;
constexpr int32 LocalErrorRealmAddress = 1006;
constexpr int32 LocalErrorCryptoFailure = 1007;

constexpr uint8 GameNameBytes[4] = { 'W', 'o', 'w', ' ' };
constexpr uint8 PlatformBytes[4] = { '6', '8', 'x', 0x00 };
constexpr uint8 OperatingSystemBytes[4] = { 'n', 'i', 'W', 0x00 };
constexpr uint8 LocaleBytes[4] = { 'S', 'U', 'n', 'e' };
constexpr uint8 ZeroBytes[4] = { 0, 0, 0, 0 };
constexpr uint8 ClientEncryptionKey[16] = {
    0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5,
    0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE
};
constexpr uint8 ClientDecryptionKey[16] = {
    0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA,
    0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57
};

struct FSocketDeleter
{
    void operator()(FSocket* Socket) const
    {
        if (Socket != nullptr)
        {
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        }
    }
};

struct FBignumDeleter
{
    void operator()(BIGNUM* Number) const
    {
        if (Number != nullptr)
        {
            BN_free(Number);
        }
    }
};

struct FBnCtxDeleter
{
    void operator()(BN_CTX* Context) const
    {
        if (Context != nullptr)
        {
            BN_CTX_free(Context);
        }
    }
};

using FSocketPtr = TUniquePtr<FSocket, FSocketDeleter>;
using FBignumPtr = TUniquePtr<BIGNUM, FBignumDeleter>;
using FBnCtxPtr = TUniquePtr<BN_CTX, FBnCtxDeleter>;

struct FAuthChallengeData
{
    TArray<uint8> ServerPublicEphemeral;
    TArray<uint8> Generator;
    TArray<uint8> Modulus;
    TArray<uint8> Salt;
    uint8 SecurityFlags = 0;
};

struct FRealmListData
{
    TArray<FUnrealAzerothRealmInfo> Realms;
};

struct FSrpClientResult
{
    TArray<uint8> PublicEphemeral;
    TArray<uint8> ClientProof;
    TArray<uint8> ExpectedServerProof;
    TArray<uint8> SessionKey;
};

struct FWorldPacket
{
    uint16 Opcode = 0;
    TArray<uint8> Payload;
};

template <typename TResult>
TResult MakeFailureResult(int32 ErrorCode, FString Message)
{
    TResult Result;
    Result.ErrorCode = ErrorCode;
    Result.ErrorMessage = MoveTemp(Message);
    return Result;
}

FString DescribeAuthResultCode(uint8 Code)
{
    switch (Code)
    {
    case 0x00:
        return TEXT("Authentication succeeded.");
    case 0x03:
        return TEXT("The account is banned.");
    case 0x04:
        return TEXT("Unknown account.");
    case 0x05:
        return TEXT("Incorrect password.");
    case 0x06:
        return TEXT("The account is already online.");
    case 0x08:
        return TEXT("The auth database is busy.");
    case 0x09:
        return TEXT("Client version mismatch.");
    case 0x0C:
        return TEXT("The account is suspended.");
    case 0x10:
        return TEXT("The account is IP locked.");
    case 0x15:
        return TEXT("No game account is available for this login.");
    case 0x19:
        return TEXT("The account is country locked.");
    default:
        return FString::Printf(TEXT("Auth server returned code 0x%02X."), Code);
    }
}

FString DescribeWorldAuthResultCode(uint8 Code)
{
    switch (Code)
    {
    case 0x0C:
        return TEXT("World authentication succeeded.");
    case 0x0D:
        return TEXT("World authentication failed.");
    case 0x0E:
        return TEXT("The world server rejected the connection.");
    case 0x10:
        return TEXT("The account is not allowed on this world server.");
    case 0x15:
        return TEXT("Unknown world account.");
    case 0x16:
        return TEXT("Incorrect password for world authentication.");
    case 0x1B:
        return TEXT("The account is queued for login.");
    case 0x1C:
        return TEXT("The account is banned from the world server.");
    case 0x1D:
        return TEXT("The account is already online on the world server.");
    case 0x1F:
        return TEXT("The world server database is busy.");
    case 0x20:
        return TEXT("The account is suspended on the world server.");
    case 0x27:
        return TEXT("The selected realm was not found by the world server.");
    default:
        return FString::Printf(TEXT("World server returned code 0x%02X."), Code);
    }
}

FString NormalizeCredential(const FString& Value)
{
    FString Uppercase = Value;
    Uppercase.ToUpperInline();
    return Uppercase;
}

TArray<uint8> MakeUtf8Bytes(const FString& Value)
{
    FTCHARToUTF8 Converter(*Value);

    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
    return Bytes;
}

FString MakeUtf8String(const TArray<uint8>& Bytes)
{
    if (Bytes.IsEmpty())
    {
        return FString();
    }

    const FUTF8ToTCHAR Converter(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
    return FString(Converter.Length(), Converter.Get());
}

void WriteUInt8(TArray<uint8>& Buffer, uint8 Value)
{
    Buffer.Add(Value);
}

void WriteUInt16LittleEndian(TArray<uint8>& Buffer, uint16 Value)
{
    Buffer.Add(static_cast<uint8>(Value & 0xFF));
    Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
}

void WriteUInt16BigEndian(TArray<uint8>& Buffer, uint16 Value)
{
    Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
    Buffer.Add(static_cast<uint8>(Value & 0xFF));
}

void WriteUInt32LittleEndian(TArray<uint8>& Buffer, uint32 Value)
{
    Buffer.Add(static_cast<uint8>(Value & 0xFF));
    Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
    Buffer.Add(static_cast<uint8>((Value >> 16) & 0xFF));
    Buffer.Add(static_cast<uint8>((Value >> 24) & 0xFF));
}

void WriteUInt64LittleEndian(TArray<uint8>& Buffer, uint64 Value)
{
    for (int32 Index = 0; Index < 8; ++Index)
    {
        Buffer.Add(static_cast<uint8>((Value >> (Index * 8)) & 0xFF));
    }
}

void WriteUtf8String(TArray<uint8>& Buffer, const FString& Value)
{
    Buffer.Append(MakeUtf8Bytes(Value));
}

void WriteUtf8CString(TArray<uint8>& Buffer, const FString& Value)
{
    WriteUtf8String(Buffer, Value);
    Buffer.Add(0);
}

TArray<uint8> MakeSha1Digest(const TArray<uint8>& Data)
{
    TArray<uint8> Digest;
    Digest.SetNumUninitialized(20);

    const void* DataPointer = Data.IsEmpty() ? nullptr : Data.GetData();
    FSHA1::HashBuffer(DataPointer, static_cast<uint32>(Data.Num()), Digest.GetData());
    return Digest;
}

TArray<uint8> MakeSha1DigestFromRaw(const uint8* Data, int32 NumBytes)
{
    TArray<uint8> Digest;
    Digest.SetNumUninitialized(20);
    FSHA1::HashBuffer(Data, static_cast<uint32>(NumBytes), Digest.GetData());
    return Digest;
}

TArray<uint8> MakeHmacSha1Digest(const TArray<uint8>& Key, const TArray<uint8>& Message)
{
    constexpr int32 BlockSize = 64;

    TArray<uint8> WorkingKey = Key;
    if (WorkingKey.Num() > BlockSize)
    {
        WorkingKey = MakeSha1Digest(WorkingKey);
    }

    WorkingKey.SetNumZeroed(BlockSize, EAllowShrinking::No);

    TArray<uint8> Inner;
    Inner.Reserve(BlockSize + Message.Num());
    for (int32 Index = 0; Index < BlockSize; ++Index)
    {
        Inner.Add(WorkingKey[Index] ^ 0x36);
    }
    Inner.Append(Message);

    const TArray<uint8> InnerDigest = MakeSha1Digest(Inner);

    TArray<uint8> Outer;
    Outer.Reserve(BlockSize + InnerDigest.Num());
    for (int32 Index = 0; Index < BlockSize; ++Index)
    {
        Outer.Add(WorkingKey[Index] ^ 0x5C);
    }
    Outer.Append(InnerDigest);

    return MakeSha1Digest(Outer);
}

bool GenerateRandomBytes(int32 NumBytes, TArray<uint8>& OutBytes)
{
    OutBytes.SetNumUninitialized(NumBytes);
    return RAND_bytes(OutBytes.GetData(), NumBytes) == 1;
}

class FByteReader
{
public:
    explicit FByteReader(const TArray<uint8>& InData)
        : Data(InData)
    {
    }

    bool ReadUInt8(uint8& OutValue)
    {
        if (Remaining() < 1)
        {
            return false;
        }

        OutValue = Data[Offset++];
        return true;
    }

    bool ReadUInt16LittleEndian(uint16& OutValue)
    {
        if (Remaining() < 2)
        {
            return false;
        }

        OutValue = static_cast<uint16>(Data[Offset]) |
            (static_cast<uint16>(Data[Offset + 1]) << 8);
        Offset += 2;
        return true;
    }

    bool ReadUInt32LittleEndian(uint32& OutValue)
    {
        if (Remaining() < 4)
        {
            return false;
        }

        OutValue =
            static_cast<uint32>(Data[Offset]) |
            (static_cast<uint32>(Data[Offset + 1]) << 8) |
            (static_cast<uint32>(Data[Offset + 2]) << 16) |
            (static_cast<uint32>(Data[Offset + 3]) << 24);
        Offset += 4;
        return true;
    }

    bool ReadUInt64LittleEndian(uint64& OutValue)
    {
        if (Remaining() < 8)
        {
            return false;
        }

        OutValue = 0;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            OutValue |= static_cast<uint64>(Data[Offset + Index]) << (Index * 8);
        }
        Offset += 8;
        return true;
    }

    bool ReadFloatLittleEndian(float& OutValue)
    {
        uint32 RawBits = 0;
        if (!ReadUInt32LittleEndian(RawBits))
        {
            return false;
        }

        FMemory::Memcpy(&OutValue, &RawBits, sizeof(float));
        return true;
    }

    bool ReadBytes(int32 NumBytes, TArray<uint8>& OutBytes)
    {
        if (Remaining() < NumBytes)
        {
            return false;
        }

        OutBytes.Reset(NumBytes);
        OutBytes.Append(Data.GetData() + Offset, NumBytes);
        Offset += NumBytes;
        return true;
    }

    bool ReadCString(FString& OutValue)
    {
        const int32 TerminatorIndex = Data.Find(0, Offset);
        if (TerminatorIndex == INDEX_NONE)
        {
            return false;
        }

        TArray<uint8> Bytes;
        Bytes.Append(Data.GetData() + Offset, TerminatorIndex - Offset);
        Offset = TerminatorIndex + 1;
        OutValue = MakeUtf8String(Bytes);
        return true;
    }

    bool Skip(int32 NumBytes)
    {
        if (Remaining() < NumBytes)
        {
            return false;
        }

        Offset += NumBytes;
        return true;
    }

    int32 Remaining() const
    {
        return Data.Num() - Offset;
    }

private:
    const TArray<uint8>& Data;
    int32 Offset = 0;
};

class FArc4
{
public:
    void Initialize(const TArray<uint8>& Key)
    {
        check(!Key.IsEmpty());

        for (int32 Index = 0; Index < 256; ++Index)
        {
            State[Index] = static_cast<uint8>(Index);
        }

        uint8 SwapIndex = 0;
        for (int32 Index = 0; Index < 256; ++Index)
        {
            SwapIndex = static_cast<uint8>(SwapIndex + State[Index] + Key[Index % Key.Num()]);
            Swap(State[Index], State[SwapIndex]);
        }

        I = 0;
        J = 0;
        bInitialized = true;
    }

    void Apply(uint8* Data, int32 NumBytes)
    {
        check(bInitialized);

        for (int32 Index = 0; Index < NumBytes; ++Index)
        {
            I = static_cast<uint8>(I + 1);
            J = static_cast<uint8>(J + State[I]);
            Swap(State[I], State[J]);

            const uint8 KeystreamIndex = static_cast<uint8>(State[I] + State[J]);
            Data[Index] ^= State[KeystreamIndex];
        }
    }

    void Drop(int32 NumBytes)
    {
        TArray<uint8> Dummy;
        Dummy.SetNumZeroed(NumBytes);
        Apply(Dummy.GetData(), Dummy.Num());
    }

private:
    uint8 State[256] = {};
    uint8 I = 0;
    uint8 J = 0;
    bool bInitialized = false;
};

class FSocketClient
{
public:
    bool Connect(const FString& Host, int32 Port, FString& OutError)
    {
        ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        const FString PortString = FString::FromInt(Port);
        const FAddressInfoResult AddressInfo = SocketSubsystem.GetAddressInfo(
            *Host,
            *PortString,
            EAddressInfoFlags::NoResolveService,
            NAME_None,
            SOCKTYPE_Streaming);

        if (AddressInfo.ReturnCode != SE_NO_ERROR || AddressInfo.Results.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Could not resolve %s:%d."), *Host, Port);
            return false;
        }

        for (const FAddressInfoResultData& Result : AddressInfo.Results)
        {
            FSocketPtr Candidate(SocketSubsystem.CreateSocket(
                Result.GetSocketTypeName(),
                TEXT("UnrealAzerothSocket"),
                Result.AddressProtocolName));

            if (!Candidate)
            {
                continue;
            }

            Candidate->SetNoDelay(true);
            Candidate->SetNonBlocking(true);

            const bool bConnectStarted = Candidate->Connect(*Result.Address);
            const ESocketErrors ConnectError = SocketSubsystem.GetLastErrorCode();
            const bool bPendingConnect = ConnectError == SE_EWOULDBLOCK || ConnectError == SE_EINPROGRESS || ConnectError == SE_NO_ERROR;

            if ((bConnectStarted || bPendingConnect) &&
                Candidate->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(ConnectTimeoutMs)) &&
                Candidate->GetConnectionState() == ESocketConnectionState::SCS_Connected)
            {
                Socket = MoveTemp(Candidate);
                return true;
            }
        }

        OutError = FString::Printf(TEXT("Failed to connect to %s:%d."), *Host, Port);
        return false;
    }

    bool SendAll(const TArray<uint8>& Data, FString& OutError)
    {
        if (!Socket)
        {
            OutError = TEXT("Socket is not connected.");
            return false;
        }

        ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

        int32 BytesSentTotal = 0;
        while (BytesSentTotal < Data.Num())
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(IoTimeoutMs)))
            {
                OutError = TEXT("Timed out while sending data to the server.");
                return false;
            }

            int32 BytesSent = 0;
            const bool bSendOk = Socket->Send(Data.GetData() + BytesSentTotal, Data.Num() - BytesSentTotal, BytesSent);
            if (!bSendOk)
            {
                const ESocketErrors ErrorCode = SocketSubsystem.GetLastErrorCode();
                if (ErrorCode == SE_EWOULDBLOCK || ErrorCode == SE_EINPROGRESS || ErrorCode == SE_TRY_AGAIN)
                {
                    continue;
                }

                OutError = FString::Printf(TEXT("Socket send failed with code %d."), static_cast<int32>(ErrorCode));
                return false;
            }

            if (BytesSent <= 0)
            {
                OutError = TEXT("Socket send returned no progress.");
                return false;
            }

            BytesSentTotal += BytesSent;
        }

        return true;
    }

    bool ReceiveExact(int32 NumBytes, TArray<uint8>& OutData, FString& OutError)
    {
        OutData.SetNumUninitialized(NumBytes);
        return ReceiveExact(OutData.GetData(), NumBytes, OutError);
    }

    bool ReceiveExact(uint8* Destination, int32 NumBytes, FString& OutError)
    {
        if (!Socket)
        {
            OutError = TEXT("Socket is not connected.");
            return false;
        }

        ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

        int32 BytesReadTotal = 0;
        while (BytesReadTotal < NumBytes)
        {
            if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(IoTimeoutMs)))
            {
                OutError = TEXT("Timed out while waiting for server data.");
                return false;
            }

            int32 BytesRead = 0;
            const bool bRecvOk = Socket->Recv(Destination + BytesReadTotal, NumBytes - BytesReadTotal, BytesRead);
            if (!bRecvOk)
            {
                const ESocketErrors ErrorCode = SocketSubsystem.GetLastErrorCode();
                if (ErrorCode == SE_EWOULDBLOCK || ErrorCode == SE_EINPROGRESS || ErrorCode == SE_TRY_AGAIN)
                {
                    continue;
                }

                OutError = FString::Printf(TEXT("Socket receive failed with code %d."), static_cast<int32>(ErrorCode));
                return false;
            }

            if (BytesRead <= 0)
            {
                OutError = TEXT("The remote server closed the connection.");
                return false;
            }

            BytesReadTotal += BytesRead;
        }

        return true;
    }

private:
    FSocketPtr Socket;
};

FBignumPtr MakeBigNumFromLittleEndian(const TArray<uint8>& LittleEndianBytes)
{
    TArray<uint8> BigEndianBytes = LittleEndianBytes;
    Algo::Reverse(BigEndianBytes);
    return FBignumPtr(BN_bin2bn(BigEndianBytes.GetData(), BigEndianBytes.Num(), nullptr));
}

TArray<uint8> MakeLittleEndianBytes(const BIGNUM* Number, int32 NumBytes)
{
    TArray<uint8> BigEndianBytes;
    BigEndianBytes.SetNumZeroed(NumBytes);

    if (BN_bn2binpad(Number, BigEndianBytes.GetData(), NumBytes) != NumBytes)
    {
        return {};
    }

    Algo::Reverse(BigEndianBytes);
    return BigEndianBytes;
}

bool ComputeClientSrp(
    const FString& AccountName,
    const FString& Password,
    const FAuthChallengeData& Challenge,
    FSrpClientResult& OutResult,
    FString& OutError)
{
    FBnCtxPtr Context(BN_CTX_new());
    if (!Context)
    {
        OutError = TEXT("Could not create OpenSSL BN context.");
        return false;
    }

    FBignumPtr Modulus = MakeBigNumFromLittleEndian(Challenge.Modulus);
    FBignumPtr Generator = MakeBigNumFromLittleEndian(Challenge.Generator);
    FBignumPtr ServerPublicEphemeral = MakeBigNumFromLittleEndian(Challenge.ServerPublicEphemeral);
    if (!Modulus || !Generator || !ServerPublicEphemeral)
    {
        OutError = TEXT("Could not parse SRP challenge values.");
        return false;
    }

    TArray<uint8> PrivateEphemeralBytes;
    if (!GenerateRandomBytes(19, PrivateEphemeralBytes))
    {
        OutError = TEXT("Could not generate random SRP bytes.");
        return false;
    }

    FBignumPtr PrivateEphemeral = MakeBigNumFromLittleEndian(PrivateEphemeralBytes);
    FBignumPtr PublicEphemeral(BN_new());
    if (!PrivateEphemeral || !PublicEphemeral)
    {
        OutError = TEXT("Could not allocate SRP temporary values.");
        return false;
    }

    if (BN_mod_exp(PublicEphemeral.Get(), Generator.Get(), PrivateEphemeral.Get(), Modulus.Get(), Context.Get()) != 1)
    {
        OutError = TEXT("Could not compute the SRP public ephemeral.");
        return false;
    }

    if (BN_is_zero(PublicEphemeral.Get()))
    {
        OutError = TEXT("The SRP public ephemeral resolved to zero.");
        return false;
    }

    OutResult.PublicEphemeral = MakeLittleEndianBytes(PublicEphemeral.Get(), 32);
    if (OutResult.PublicEphemeral.Num() != 32)
    {
        OutError = TEXT("Could not serialize the SRP public ephemeral.");
        return false;
    }

    const TArray<uint8> AccountBytes = MakeUtf8Bytes(AccountName);
    const TArray<uint8> PasswordBytes = MakeUtf8Bytes(Password);

    TArray<uint8> CredentialsHashInput;
    CredentialsHashInput.Reserve(AccountBytes.Num() + PasswordBytes.Num() + 1);
    CredentialsHashInput.Append(AccountBytes);
    CredentialsHashInput.Add(':');
    CredentialsHashInput.Append(PasswordBytes);
    const TArray<uint8> CredentialsHash = MakeSha1Digest(CredentialsHashInput);

    TArray<uint8> SaltedHashInput;
    SaltedHashInput.Reserve(Challenge.Salt.Num() + CredentialsHash.Num());
    SaltedHashInput.Append(Challenge.Salt);
    SaltedHashInput.Append(CredentialsHash);
    const TArray<uint8> XDigest = MakeSha1Digest(SaltedHashInput);
    FBignumPtr X = MakeBigNumFromLittleEndian(XDigest);
    if (!X)
    {
        OutError = TEXT("Could not compute the SRP x value.");
        return false;
    }

    TArray<uint8> ScramblingHashInput;
    ScramblingHashInput.Reserve(OutResult.PublicEphemeral.Num() + Challenge.ServerPublicEphemeral.Num());
    ScramblingHashInput.Append(OutResult.PublicEphemeral);
    ScramblingHashInput.Append(Challenge.ServerPublicEphemeral);
    const TArray<uint8> UDigest = MakeSha1Digest(ScramblingHashInput);
    FBignumPtr U = MakeBigNumFromLittleEndian(UDigest);
    if (!U)
    {
        OutError = TEXT("Could not compute the SRP scrambling parameter.");
        return false;
    }

    FBignumPtr K(BN_new());
    if (!K || BN_set_word(K.Get(), 3) != 1)
    {
        OutError = TEXT("Could not create the SRP multiplier.");
        return false;
    }

    FBignumPtr GeneratorPowX(BN_new());
    FBignumPtr Kgx(BN_new());
    FBignumPtr Base(BN_new());
    FBignumPtr Ux(BN_new());
    FBignumPtr Exponent(BN_new());
    FBignumPtr SessionSecret(BN_new());
    if (!GeneratorPowX || !Kgx || !Base || !Ux || !Exponent || !SessionSecret)
    {
        OutError = TEXT("Could not allocate SRP working numbers.");
        return false;
    }

    if (BN_mod_exp(GeneratorPowX.Get(), Generator.Get(), X.Get(), Modulus.Get(), Context.Get()) != 1)
    {
        OutError = TEXT("Could not compute g^x for SRP.");
        return false;
    }

    if (BN_mul(Kgx.Get(), K.Get(), GeneratorPowX.Get(), Context.Get()) != 1)
    {
        OutError = TEXT("Could not compute k*g^x for SRP.");
        return false;
    }

    if (BN_mod_sub(Base.Get(), ServerPublicEphemeral.Get(), Kgx.Get(), Modulus.Get(), Context.Get()) != 1)
    {
        OutError = TEXT("Could not compute the SRP base value.");
        return false;
    }

    if (BN_mul(Ux.Get(), U.Get(), X.Get(), Context.Get()) != 1 || BN_add(Exponent.Get(), PrivateEphemeral.Get(), Ux.Get()) != 1)
    {
        OutError = TEXT("Could not compute the SRP exponent.");
        return false;
    }

    if (BN_mod_exp(SessionSecret.Get(), Base.Get(), Exponent.Get(), Modulus.Get(), Context.Get()) != 1)
    {
        OutError = TEXT("Could not compute the SRP session secret.");
        return false;
    }

    const TArray<uint8> SessionSecretBytes = MakeLittleEndianBytes(SessionSecret.Get(), 32);
    if (SessionSecretBytes.Num() != 32)
    {
        OutError = TEXT("Could not serialize the SRP session secret.");
        return false;
    }

    TArray<uint8> EvenHalf;
    TArray<uint8> OddHalf;
    EvenHalf.Reserve(16);
    OddHalf.Reserve(16);

    int32 FirstNonZero = 0;
    while (FirstNonZero < SessionSecretBytes.Num() && SessionSecretBytes[FirstNonZero] == 0)
    {
        ++FirstNonZero;
    }

    if ((FirstNonZero & 1) != 0)
    {
        ++FirstNonZero;
    }

    const int32 HalfOffset = FirstNonZero / 2;
    for (int32 Index = 0; Index < 16; ++Index)
    {
        EvenHalf.Add(SessionSecretBytes[Index * 2]);
        OddHalf.Add(SessionSecretBytes[Index * 2 + 1]);
    }

    const uint8* EvenStart = EvenHalf.GetData() + FMath::Min(HalfOffset, EvenHalf.Num());
    const uint8* OddStart = OddHalf.GetData() + FMath::Min(HalfOffset, OddHalf.Num());
    const int32 EvenLength = EvenHalf.Num() - FMath::Min(HalfOffset, EvenHalf.Num());
    const int32 OddLength = OddHalf.Num() - FMath::Min(HalfOffset, OddHalf.Num());

    const TArray<uint8> EvenDigest = MakeSha1DigestFromRaw(EvenStart, EvenLength);
    const TArray<uint8> OddDigest = MakeSha1DigestFromRaw(OddStart, OddLength);

    OutResult.SessionKey.SetNumUninitialized(40);
    for (int32 Index = 0; Index < 20; ++Index)
    {
        OutResult.SessionKey[Index * 2] = EvenDigest[Index];
        OutResult.SessionKey[Index * 2 + 1] = OddDigest[Index];
    }

    const TArray<uint8> UserHash = MakeSha1Digest(AccountBytes);
    const TArray<uint8> ModulusHash = MakeSha1Digest(Challenge.Modulus);
    const TArray<uint8> GeneratorHash = MakeSha1Digest(Challenge.Generator);

    TArray<uint8> ModulusGeneratorXor;
    ModulusGeneratorXor.SetNumUninitialized(20);
    for (int32 Index = 0; Index < 20; ++Index)
    {
        ModulusGeneratorXor[Index] = ModulusHash[Index] ^ GeneratorHash[Index];
    }

    TArray<uint8> ClientProofInput;
    ClientProofInput.Reserve(
        ModulusGeneratorXor.Num() +
        UserHash.Num() +
        Challenge.Salt.Num() +
        OutResult.PublicEphemeral.Num() +
        Challenge.ServerPublicEphemeral.Num() +
        OutResult.SessionKey.Num());
    ClientProofInput.Append(ModulusGeneratorXor);
    ClientProofInput.Append(UserHash);
    ClientProofInput.Append(Challenge.Salt);
    ClientProofInput.Append(OutResult.PublicEphemeral);
    ClientProofInput.Append(Challenge.ServerPublicEphemeral);
    ClientProofInput.Append(OutResult.SessionKey);
    OutResult.ClientProof = MakeSha1Digest(ClientProofInput);

    TArray<uint8> ServerProofInput;
    ServerProofInput.Reserve(OutResult.PublicEphemeral.Num() + OutResult.ClientProof.Num() + OutResult.SessionKey.Num());
    ServerProofInput.Append(OutResult.PublicEphemeral);
    ServerProofInput.Append(OutResult.ClientProof);
    ServerProofInput.Append(OutResult.SessionKey);
    OutResult.ExpectedServerProof = MakeSha1Digest(ServerProofInput);
    return true;
}

bool ParseRealmAddress(const FString& Address, FString& OutHost, int32& OutPort)
{
    FString Host;
    FString PortString;
    if (!Address.Split(TEXT(":"), &Host, &PortString, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
    {
        return false;
    }

    Host.TrimStartAndEndInline();
    PortString.TrimStartAndEndInline();

    int32 Port = 0;
    if (Host.IsEmpty() || PortString.IsEmpty() || !LexTryParseString(Port, *PortString))
    {
        return false;
    }

    OutHost = MoveTemp(Host);
    OutPort = Port;
    return true;
}

void InitializeWorldCrypt(const TArray<uint8>& SessionKey, FArc4& OutEncryptor, FArc4& OutDecryptor)
{
    TArray<uint8> EncryptKey;
    EncryptKey.Append(ClientEncryptionKey, UE_ARRAY_COUNT(ClientEncryptionKey));
    const TArray<uint8> EncryptDigest = MakeHmacSha1Digest(EncryptKey, SessionKey);

    TArray<uint8> DecryptKey;
    DecryptKey.Append(ClientDecryptionKey, UE_ARRAY_COUNT(ClientDecryptionKey));
    const TArray<uint8> DecryptDigest = MakeHmacSha1Digest(DecryptKey, SessionKey);

    OutEncryptor.Initialize(EncryptDigest);
    OutDecryptor.Initialize(DecryptDigest);
    OutEncryptor.Drop(1024);
    OutDecryptor.Drop(1024);
}

bool SendAuthLogonChallenge(
    FSocketClient& SocketClient,
    const FString& AccountName,
    int32 AuthServerPort,
    FString& OutError)
{
    TArray<uint8> Packet;
    const TArray<uint8> AccountBytes = MakeUtf8Bytes(AccountName);

    Packet.Reserve(1 + 1 + 2 + 4 + 3 + 2 + 4 + 4 + 4 + 4 + 4 + 1 + AccountBytes.Num());
    WriteUInt8(Packet, AuthLogonChallengeOpcode);
    WriteUInt8(Packet, 0x00);
    WriteUInt16LittleEndian(Packet, static_cast<uint16>(30 + AccountBytes.Num()));
    Packet.Append(GameNameBytes, UE_ARRAY_COUNT(GameNameBytes));
    WriteUInt8(Packet, 3);
    WriteUInt8(Packet, 3);
    WriteUInt8(Packet, 5);
    WriteUInt16LittleEndian(Packet, WowClientBuild);
    Packet.Append(PlatformBytes, UE_ARRAY_COUNT(PlatformBytes));
    Packet.Append(OperatingSystemBytes, UE_ARRAY_COUNT(OperatingSystemBytes));
    Packet.Append(LocaleBytes, UE_ARRAY_COUNT(LocaleBytes));
    WriteUInt32LittleEndian(Packet, 0);
    WriteUInt32LittleEndian(Packet, 0);
    WriteUInt8(Packet, static_cast<uint8>(AccountBytes.Num()));
    Packet.Append(AccountBytes);

    UE_LOG(LogUnrealAzeroth, Display, TEXT("Connecting to authserver on port %d for account %s."), AuthServerPort, *AccountName);
    return SocketClient.SendAll(Packet, OutError);
}

bool ReceiveAuthChallengeResponse(FSocketClient& SocketClient, FAuthChallengeData& OutChallenge, FString& OutError, int32& OutErrorCode)
{
    TArray<uint8> Header;
    if (!SocketClient.ReceiveExact(1, Header, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    if (Header[0] != AuthLogonChallengeOpcode)
    {
        OutErrorCode = LocalErrorProtocolFailure;
        OutError = FString::Printf(TEXT("Unexpected auth opcode 0x%02X during logon challenge."), Header[0]);
        return false;
    }

    TArray<uint8> Prefix;
    if (!SocketClient.ReceiveExact(2, Prefix, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    const uint8 ResultCode = Prefix[1];
    if (ResultCode != AuthSuccessCode)
    {
        OutErrorCode = ResultCode;
        OutError = DescribeAuthResultCode(ResultCode);
        return false;
    }

    if (!SocketClient.ReceiveExact(32, OutChallenge.ServerPublicEphemeral, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    TArray<uint8> LengthBuffer;
    if (!SocketClient.ReceiveExact(1, LengthBuffer, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    const int32 GeneratorLength = LengthBuffer[0];
    if (!SocketClient.ReceiveExact(GeneratorLength, OutChallenge.Generator, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    if (!SocketClient.ReceiveExact(1, LengthBuffer, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    const int32 ModulusLength = LengthBuffer[0];
    if (!SocketClient.ReceiveExact(ModulusLength, OutChallenge.Modulus, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    if (!SocketClient.ReceiveExact(32, OutChallenge.Salt, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    TArray<uint8> VersionChallenge;
    if (!SocketClient.ReceiveExact(16, VersionChallenge, OutError) || !SocketClient.ReceiveExact(1, LengthBuffer, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    OutChallenge.SecurityFlags = LengthBuffer[0];

    int32 ExtraSecurityBytes = 0;
    if ((OutChallenge.SecurityFlags & 0x01) != 0)
    {
        ExtraSecurityBytes += 4 + 16;
    }
    if ((OutChallenge.SecurityFlags & 0x02) != 0)
    {
        ExtraSecurityBytes += 1 + 1 + 1 + 1 + 8;
    }
    if ((OutChallenge.SecurityFlags & 0x04) != 0)
    {
        ExtraSecurityBytes += 1;
    }

    if (ExtraSecurityBytes > 0)
    {
        TArray<uint8> IgnoredBytes;
        if (!SocketClient.ReceiveExact(ExtraSecurityBytes, IgnoredBytes, OutError))
        {
            OutErrorCode = LocalErrorNetworkFailure;
            return false;
        }
    }

    return true;
}

bool SendAuthLogonProof(FSocketClient& SocketClient, const FSrpClientResult& SrpResult, FString& OutError)
{
    TArray<uint8> Packet;
    Packet.Reserve(1 + SrpResult.PublicEphemeral.Num() + SrpResult.ClientProof.Num() + 20 + 2);
    WriteUInt8(Packet, AuthLogonProofOpcode);
    Packet.Append(SrpResult.PublicEphemeral);
    Packet.Append(SrpResult.ClientProof);

    TArray<uint8> CrcHash;
    CrcHash.SetNumZeroed(20);
    Packet.Append(CrcHash);
    WriteUInt8(Packet, 0x00);
    WriteUInt8(Packet, 0x00);
    return SocketClient.SendAll(Packet, OutError);
}

bool ReceiveAuthLogonProofResponse(FSocketClient& SocketClient, const FSrpClientResult& SrpResult, FString& OutError, int32& OutErrorCode)
{
    TArray<uint8> OpcodeBuffer;
    if (!SocketClient.ReceiveExact(1, OpcodeBuffer, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    if (OpcodeBuffer[0] != AuthLogonProofOpcode)
    {
        OutErrorCode = LocalErrorProtocolFailure;
        OutError = FString::Printf(TEXT("Unexpected auth opcode 0x%02X during logon proof."), OpcodeBuffer[0]);
        return false;
    }

    TArray<uint8> ResultBuffer;
    if (!SocketClient.ReceiveExact(1, ResultBuffer, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    const uint8 ResultCode = ResultBuffer[0];
    if (ResultCode != AuthSuccessCode)
    {
        TArray<uint8> IgnoredTail;
        SocketClient.ReceiveExact(2, IgnoredTail, OutError);
        OutErrorCode = ResultCode;
        OutError = DescribeAuthResultCode(ResultCode);
        return false;
    }

    TArray<uint8> Payload;
    if (!SocketClient.ReceiveExact(30, Payload, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    TArray<uint8> ServerProof;
    ServerProof.Append(Payload.GetData(), 20);
    if (ServerProof != SrpResult.ExpectedServerProof)
    {
        OutErrorCode = LocalErrorCryptoFailure;
        OutError = TEXT("The auth server returned an invalid SRP proof.");
        return false;
    }

    return true;
}

bool SendRealmListRequest(FSocketClient& SocketClient, FString& OutError)
{
    TArray<uint8> Packet;
    Packet.Reserve(5);
    WriteUInt8(Packet, AuthRealmListOpcode);
    WriteUInt32LittleEndian(Packet, 0);
    return SocketClient.SendAll(Packet, OutError);
}

bool ReceiveRealmListResponse(FSocketClient& SocketClient, FRealmListData& OutRealmList, FString& OutError, int32& OutErrorCode)
{
    TArray<uint8> Header;
    if (!SocketClient.ReceiveExact(3, Header, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    if (Header[0] != AuthRealmListOpcode)
    {
        OutErrorCode = LocalErrorProtocolFailure;
        OutError = FString::Printf(TEXT("Unexpected auth opcode 0x%02X during realm list."), Header[0]);
        return false;
    }

    const int32 PayloadBytes = static_cast<int32>(Header[1]) | (static_cast<int32>(Header[2]) << 8);
    TArray<uint8> Payload;
    if (!SocketClient.ReceiveExact(PayloadBytes, Payload, OutError))
    {
        OutErrorCode = LocalErrorNetworkFailure;
        return false;
    }

    FByteReader Reader(Payload);
    uint32 Unused = 0;
    uint16 RealmCount = 0;
    if (!Reader.ReadUInt32LittleEndian(Unused) || !Reader.ReadUInt16LittleEndian(RealmCount))
    {
        OutErrorCode = LocalErrorProtocolFailure;
        OutError = TEXT("The realm list response was truncated.");
        return false;
    }

    OutRealmList.Realms.Reserve(RealmCount);
    for (uint16 RealmIndex = 0; RealmIndex < RealmCount; ++RealmIndex)
    {
        FUnrealAzerothRealmInfo RealmInfo;

        uint8 RealmType = 0;
        uint8 Locked = 0;
        uint8 Flags = 0;
        uint8 CharacterCount = 0;
        uint8 Timezone = 0;
        uint8 RealmId = 0;

        if (!Reader.ReadUInt8(RealmType) ||
            !Reader.ReadUInt8(Locked) ||
            !Reader.ReadUInt8(Flags) ||
            !Reader.ReadCString(RealmInfo.Name) ||
            !Reader.ReadCString(RealmInfo.Address))
        {
            OutErrorCode = LocalErrorProtocolFailure;
            OutError = TEXT("Could not parse a realm list entry.");
            return false;
        }

        if (!Reader.ReadFloatLittleEndian(RealmInfo.Population) ||
            !Reader.ReadUInt8(CharacterCount) ||
            !Reader.ReadUInt8(Timezone) ||
            !Reader.ReadUInt8(RealmId))
        {
            OutErrorCode = LocalErrorProtocolFailure;
            OutError = TEXT("Could not finish reading a realm list entry.");
            return false;
        }

        RealmInfo.RealmType = RealmType;
        RealmInfo.Flags = Flags;
        RealmInfo.CharacterCount = CharacterCount;
        RealmInfo.Timezone = Timezone;
        RealmInfo.RealmId = RealmId;
        RealmInfo.bLocked = Locked != 0;
        RealmInfo.bOffline = (Flags & RealmFlagOffline) != 0;
        RealmInfo.bSpecifiesBuild = (Flags & RealmFlagSpecifyBuild) != 0;

        if (RealmInfo.bSpecifiesBuild)
        {
            uint8 BuildMajor = 0;
            uint8 BuildMinor = 0;
            uint8 BuildPatch = 0;
            uint16 BuildNumber = 0;
            if (!Reader.ReadUInt8(BuildMajor) ||
                !Reader.ReadUInt8(BuildMinor) ||
                !Reader.ReadUInt8(BuildPatch) ||
                !Reader.ReadUInt16LittleEndian(BuildNumber))
            {
                OutErrorCode = LocalErrorProtocolFailure;
                OutError = TEXT("Could not parse the realm build information.");
                return false;
            }

            RealmInfo.BuildMajor = BuildMajor;
            RealmInfo.BuildMinor = BuildMinor;
            RealmInfo.BuildPatch = BuildPatch;
            RealmInfo.BuildNumber = BuildNumber;
        }

        ParseRealmAddress(RealmInfo.Address, RealmInfo.Host, RealmInfo.Port);
        OutRealmList.Realms.Add(MoveTemp(RealmInfo));
    }

    return true;
}

bool SendWorldPacket(
    FSocketClient& SocketClient,
    uint32 Opcode,
    const TArray<uint8>& Payload,
    bool bEncryptHeader,
    FArc4* Encryptor,
    FString& OutError)
{
    TArray<uint8> Packet;
    Packet.Reserve(6 + Payload.Num());
    WriteUInt16BigEndian(Packet, static_cast<uint16>(Payload.Num() + 4));
    WriteUInt32LittleEndian(Packet, Opcode);
    if (bEncryptHeader)
    {
        check(Encryptor != nullptr);
        Encryptor->Apply(Packet.GetData(), 6);
    }

    Packet.Append(Payload);
    return SocketClient.SendAll(Packet, OutError);
}

bool ReceiveWorldPacket(
    FSocketClient& SocketClient,
    bool bEncryptedHeader,
    FArc4* Decryptor,
    FWorldPacket& OutPacket,
    FString& OutError)
{
    TArray<uint8> Header;
    if (!SocketClient.ReceiveExact(4, Header, OutError))
    {
        return false;
    }

    if (bEncryptedHeader)
    {
        check(Decryptor != nullptr);
        Decryptor->Apply(Header.GetData(), Header.Num());
    }

    bool bLargePacket = (Header[0] & 0x80) != 0;
    if (bLargePacket)
    {
        TArray<uint8> ExtraByte;
        if (!SocketClient.ReceiveExact(1, ExtraByte, OutError))
        {
            return false;
        }

        if (bEncryptedHeader)
        {
            Decryptor->Apply(ExtraByte.GetData(), ExtraByte.Num());
        }

        Header.Append(ExtraByte);
    }

    uint32 SizeWithOpcode = 0;
    if (bLargePacket)
    {
        SizeWithOpcode =
            (static_cast<uint32>(Header[0] & 0x7F) << 16) |
            (static_cast<uint32>(Header[1]) << 8) |
            static_cast<uint32>(Header[2]);
        OutPacket.Opcode = static_cast<uint16>(Header[3]) | (static_cast<uint16>(Header[4]) << 8);
    }
    else
    {
        SizeWithOpcode = (static_cast<uint32>(Header[0]) << 8) | static_cast<uint32>(Header[1]);
        OutPacket.Opcode = static_cast<uint16>(Header[2]) | (static_cast<uint16>(Header[3]) << 8);
    }

    if (SizeWithOpcode < 2 || SizeWithOpcode > MaxWorldPacketSizeWithOpcode)
    {
        OutError = FString::Printf(TEXT("Invalid world packet size: %u."), SizeWithOpcode);
        return false;
    }

    const int32 PayloadSize = static_cast<int32>(SizeWithOpcode - 2);
    return SocketClient.ReceiveExact(PayloadSize, OutPacket.Payload, OutError);
}

bool ParseWorldAuthChallenge(const FWorldPacket& Packet, TArray<uint8>& OutServerSeed, FString& OutError)
{
    if (Packet.Opcode != SmsgAuthChallenge)
    {
        OutError = FString::Printf(TEXT("Expected SMSG_AUTH_CHALLENGE, got 0x%04X."), Packet.Opcode);
        return false;
    }

    FByteReader Reader(Packet.Payload);
    uint32 Unknown = 0;
    if (!Reader.ReadUInt32LittleEndian(Unknown) || !Reader.ReadBytes(4, OutServerSeed))
    {
        OutError = TEXT("The world auth challenge payload was truncated.");
        return false;
    }

    return true;
}

TArray<uint8> BuildWorldDigest(
    const FString& AccountName,
    const TArray<uint8>& ClientSeed,
    const TArray<uint8>& ServerSeed,
    const TArray<uint8>& SessionKey)
{
    TArray<uint8> DigestInput;
    DigestInput.Reserve(AccountName.Len() + UE_ARRAY_COUNT(ZeroBytes) + ClientSeed.Num() + ServerSeed.Num() + SessionKey.Num());
    DigestInput.Append(MakeUtf8Bytes(AccountName));
    DigestInput.Append(ZeroBytes, UE_ARRAY_COUNT(ZeroBytes));
    DigestInput.Append(ClientSeed);
    DigestInput.Append(ServerSeed);
    DigestInput.Append(SessionKey);
    return MakeSha1Digest(DigestInput);
}

bool SendWorldAuthSession(
    FSocketClient& SocketClient,
    const UUnrealAzerothSession* Session,
    const FUnrealAzerothRealmInfo& Realm,
    const TArray<uint8>& ServerSeed,
    FString& OutError)
{
    TArray<uint8> ClientSeed;
    if (!GenerateRandomBytes(4, ClientSeed))
    {
        OutError = TEXT("Could not generate a world authentication seed.");
        return false;
    }

    const TArray<uint8> Digest = BuildWorldDigest(Session->AccountName, ClientSeed, ServerSeed, Session->GetSessionKey());

    TArray<uint8> Payload;
    Payload.Reserve(4 + 4 + Session->AccountName.Len() + 1 + 4 + 4 + 4 + 4 + 8 + Digest.Num() + 4);
    WriteUInt32LittleEndian(Payload, WowClientBuild);
    WriteUInt32LittleEndian(Payload, 0);
    WriteUtf8CString(Payload, Session->AccountName);
    WriteUInt32LittleEndian(Payload, 0);
    Payload.Append(ClientSeed);
    WriteUInt32LittleEndian(Payload, 0);
    WriteUInt32LittleEndian(Payload, 0);
    WriteUInt32LittleEndian(Payload, static_cast<uint32>(Realm.RealmId));
    WriteUInt64LittleEndian(Payload, 0);
    Payload.Append(Digest);
    WriteUInt32LittleEndian(Payload, 0);

    return SendWorldPacket(SocketClient, CmsgAuthSession, Payload, false, nullptr, OutError);
}

bool SendCharEnumRequest(FSocketClient& SocketClient, FArc4& Encryptor, FString& OutError)
{
    TArray<uint8> EmptyPayload;
    return SendWorldPacket(SocketClient, CmsgCharEnum, EmptyPayload, true, &Encryptor, OutError);
}

bool ParseCharacterList(const FWorldPacket& Packet, TArray<FUnrealAzerothCharacterSummary>& OutCharacters, FString& OutError)
{
    if (Packet.Opcode != SmsgCharEnum)
    {
        OutError = FString::Printf(TEXT("Expected SMSG_CHAR_ENUM, got 0x%04X."), Packet.Opcode);
        return false;
    }

    FByteReader Reader(Packet.Payload);
    uint8 CharacterCount = 0;
    if (!Reader.ReadUInt8(CharacterCount))
    {
        OutError = TEXT("Character enumeration response was truncated.");
        return false;
    }

    OutCharacters.Reserve(CharacterCount);
    for (uint8 CharacterIndex = 0; CharacterIndex < CharacterCount; ++CharacterIndex)
    {
        FUnrealAzerothCharacterSummary Character;

        uint64 Guid = 0;
        uint8 Race = 0;
        uint8 Class = 0;
        uint8 Gender = 0;
        uint32 IgnoredBytes = 0;
        uint8 IgnoredFacial = 0;
        uint8 Level = 0;
        uint32 Zone = 0;
        uint32 Map = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        uint32 Guild = 0;
        uint32 Flags = 0;
        uint32 CharacterCustomization = 0;
        uint8 FirstLoginFlags = 0;
        uint32 PetModel = 0;
        uint32 PetLevel = 0;
        uint32 PetFamily = 0;

        if (!Reader.ReadUInt64LittleEndian(Guid) ||
            !Reader.ReadCString(Character.Name) ||
            !Reader.ReadUInt8(Race) ||
            !Reader.ReadUInt8(Class) ||
            !Reader.ReadUInt8(Gender) ||
            !Reader.ReadUInt32LittleEndian(IgnoredBytes) ||
            !Reader.ReadUInt8(IgnoredFacial) ||
            !Reader.ReadUInt8(Level) ||
            !Reader.ReadUInt32LittleEndian(Zone) ||
            !Reader.ReadUInt32LittleEndian(Map) ||
            !Reader.ReadFloatLittleEndian(X) ||
            !Reader.ReadFloatLittleEndian(Y) ||
            !Reader.ReadFloatLittleEndian(Z) ||
            !Reader.ReadUInt32LittleEndian(Guild) ||
            !Reader.ReadUInt32LittleEndian(Flags) ||
            !Reader.ReadUInt32LittleEndian(CharacterCustomization) ||
            !Reader.ReadUInt8(FirstLoginFlags) ||
            !Reader.ReadUInt32LittleEndian(PetModel) ||
            !Reader.ReadUInt32LittleEndian(PetLevel) ||
            !Reader.ReadUInt32LittleEndian(PetFamily))
        {
            OutError = TEXT("Character enumeration data was truncated.");
            return false;
        }

        for (int32 EquipmentIndex = 0; EquipmentIndex < 23; ++EquipmentIndex)
        {
            if (!Reader.Skip(4 + 1 + 4))
            {
                OutError = TEXT("Character equipment data was truncated.");
                return false;
            }
        }

        Character.GuidHex = FString::Printf(TEXT("0x%016llX"), static_cast<unsigned long long>(Guid));
        Character.Race = Race;
        Character.Class = Class;
        Character.Gender = Gender;
        Character.Level = Level;
        Character.Zone = static_cast<int32>(Zone);
        Character.Map = static_cast<int32>(Map);
        Character.Position = FVector(X, Y, Z);
        Character.bHasPet = PetModel != 0 || PetLevel != 0 || PetFamily != 0;
        OutCharacters.Add(MoveTemp(Character));
    }

    return true;
}
}

namespace UnrealAzeroth::Auth
{
FLoginResult LoginToConfiguredServer(const FString& Username, const FString& Password)
{
    const UUnrealAzerothSettings* Settings = GetDefault<UUnrealAzerothSettings>();
    if (Settings == nullptr)
    {
        return MakeFailureResult<FLoginResult>(LocalErrorInvalidConfiguration, TEXT("Unreal Azeroth settings are not available."));
    }

    const FString AccountName = NormalizeCredential(Username);
    const FString AccountPassword = NormalizeCredential(Password);
    const FString ServerHost = Settings->ServerHost;

    if (AccountName.IsEmpty())
    {
        return MakeFailureResult<FLoginResult>(LocalErrorInvalidConfiguration, TEXT("A username is required."));
    }

    if (AccountPassword.IsEmpty())
    {
        return MakeFailureResult<FLoginResult>(LocalErrorInvalidConfiguration, TEXT("A password is required."));
    }

    if (ServerHost.IsEmpty())
    {
        return MakeFailureResult<FLoginResult>(LocalErrorInvalidConfiguration, TEXT("Server IP address is not configured in Unreal Azeroth settings."));
    }

    if (Settings->AuthServerPort < 1 || Settings->AuthServerPort > 65535)
    {
        return MakeFailureResult<FLoginResult>(LocalErrorInvalidConfiguration, TEXT("Auth server port is invalid in Unreal Azeroth settings."));
    }

    FSocketClient SocketClient;
    FString ErrorMessage;
    if (!SocketClient.Connect(ServerHost, Settings->AuthServerPort, ErrorMessage))
    {
        UE_LOG(LogUnrealAzeroth, Error, TEXT("Auth connection failed: %s"), *ErrorMessage);
        return MakeFailureResult<FLoginResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    if (!SendAuthLogonChallenge(SocketClient, AccountName, Settings->AuthServerPort, ErrorMessage))
    {
        return MakeFailureResult<FLoginResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    FAuthChallengeData Challenge;
    int32 ErrorCode = 0;
    if (!ReceiveAuthChallengeResponse(SocketClient, Challenge, ErrorMessage, ErrorCode))
    {
        UE_LOG(LogUnrealAzeroth, Warning, TEXT("Auth challenge failed for %s: %s"), *AccountName, *ErrorMessage);
        return MakeFailureResult<FLoginResult>(ErrorCode, MoveTemp(ErrorMessage));
    }

    if (Challenge.SecurityFlags != 0)
    {
        ErrorMessage = FString::Printf(
            TEXT("The auth account requires unsupported security flags (0x%02X)."),
            Challenge.SecurityFlags);
        return MakeFailureResult<FLoginResult>(LocalErrorUnsupportedSecurity, MoveTemp(ErrorMessage));
    }

    FSrpClientResult SrpResult;
    if (!ComputeClientSrp(AccountName, AccountPassword, Challenge, SrpResult, ErrorMessage))
    {
        UE_LOG(LogUnrealAzeroth, Error, TEXT("SRP calculation failed for %s: %s"), *AccountName, *ErrorMessage);
        return MakeFailureResult<FLoginResult>(LocalErrorCryptoFailure, MoveTemp(ErrorMessage));
    }

    if (!SendAuthLogonProof(SocketClient, SrpResult, ErrorMessage))
    {
        return MakeFailureResult<FLoginResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    if (!ReceiveAuthLogonProofResponse(SocketClient, SrpResult, ErrorMessage, ErrorCode))
    {
        UE_LOG(LogUnrealAzeroth, Warning, TEXT("Auth proof failed for %s: %s"), *AccountName, *ErrorMessage);
        return MakeFailureResult<FLoginResult>(ErrorCode, MoveTemp(ErrorMessage));
    }

    if (!SendRealmListRequest(SocketClient, ErrorMessage))
    {
        return MakeFailureResult<FLoginResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    FRealmListData RealmList;
    if (!ReceiveRealmListResponse(SocketClient, RealmList, ErrorMessage, ErrorCode))
    {
        return MakeFailureResult<FLoginResult>(ErrorCode, MoveTemp(ErrorMessage));
    }

    FLoginResult Result;
    Result.bSucceeded = true;
    Result.AccountName = AccountName;
    Result.ServerHost = ServerHost;
    Result.AuthServerPort = Settings->AuthServerPort;
    Result.SessionKey = MoveTemp(SrpResult.SessionKey);
    Result.Realms = MoveTemp(RealmList.Realms);

    UE_LOG(LogUnrealAzeroth, Display, TEXT("Auth login succeeded for %s. Realm count: %d"), *AccountName, Result.Realms.Num());
    return Result;
}

FCharacterListResult FetchCharactersForRealm(const UUnrealAzerothSession* Session, const FUnrealAzerothRealmInfo& Realm)
{
    if (Session == nullptr || !Session->HasSessionKey())
    {
        return MakeFailureResult<FCharacterListResult>(LocalErrorInvalidSession, TEXT("A valid authenticated session is required."));
    }

    FString RealmHost = Realm.Host;
    int32 RealmPort = Realm.Port;
    if (RealmHost.IsEmpty() || RealmPort <= 0)
    {
        if (!ParseRealmAddress(Realm.Address, RealmHost, RealmPort))
        {
            return MakeFailureResult<FCharacterListResult>(LocalErrorRealmAddress, TEXT("The selected realm does not contain a valid host:port address."));
        }
    }

    FSocketClient SocketClient;
    FString ErrorMessage;
    if (!SocketClient.Connect(RealmHost, RealmPort, ErrorMessage))
    {
        return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    UE_LOG(
        LogUnrealAzeroth,
        Display,
        TEXT("Connecting to realm %s at %s:%d for character enumeration."),
        *Realm.Name,
        *RealmHost,
        RealmPort);

    FWorldPacket Packet;
    if (!ReceiveWorldPacket(SocketClient, false, nullptr, Packet, ErrorMessage))
    {
        return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    TArray<uint8> ServerSeed;
    if (!ParseWorldAuthChallenge(Packet, ServerSeed, ErrorMessage))
    {
        return MakeFailureResult<FCharacterListResult>(LocalErrorProtocolFailure, MoveTemp(ErrorMessage));
    }

    if (!SendWorldAuthSession(SocketClient, Session, Realm, ServerSeed, ErrorMessage))
    {
        return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
    }

    FArc4 Encryptor;
    FArc4 Decryptor;
    InitializeWorldCrypt(Session->GetSessionKey(), Encryptor, Decryptor);

    bool bSentCharEnum = false;
    for (int32 PacketIndex = 0; PacketIndex < 32; ++PacketIndex)
    {
        FWorldPacket IncomingPacket;
        if (!ReceiveWorldPacket(SocketClient, true, &Decryptor, IncomingPacket, ErrorMessage))
        {
            return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
        }

        if (IncomingPacket.Opcode == SmsgAuthResponse)
        {
            if (IncomingPacket.Payload.IsEmpty())
            {
                return MakeFailureResult<FCharacterListResult>(LocalErrorProtocolFailure, TEXT("World auth response was empty."));
            }

            const uint8 ResultCode = IncomingPacket.Payload[0];
            if (ResultCode != WorldAuthOk)
            {
                return MakeFailureResult<FCharacterListResult>(ResultCode, DescribeWorldAuthResultCode(ResultCode));
            }

            if (!SendCharEnumRequest(SocketClient, Encryptor, ErrorMessage))
            {
                return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, MoveTemp(ErrorMessage));
            }

            bSentCharEnum = true;
            continue;
        }

        if (IncomingPacket.Opcode == SmsgCharEnum)
        {
            FCharacterListResult Result;
            if (!ParseCharacterList(IncomingPacket, Result.Characters, ErrorMessage))
            {
                return MakeFailureResult<FCharacterListResult>(LocalErrorProtocolFailure, MoveTemp(ErrorMessage));
            }

            Result.bSucceeded = true;
            UE_LOG(
                LogUnrealAzeroth,
                Display,
                TEXT("Received %d character(s) from realm %s."),
                Result.Characters.Num(),
                *Realm.Name);
            return Result;
        }
    }

    const FString TimeoutMessage = bSentCharEnum
        ? TEXT("Timed out while waiting for the character list from the realm.")
        : TEXT("Timed out while waiting for the world auth response.");
    return MakeFailureResult<FCharacterListResult>(LocalErrorNetworkFailure, TimeoutMessage);
}
}
