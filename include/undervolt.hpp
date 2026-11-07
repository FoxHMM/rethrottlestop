

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace uv {

enum class Plane : int {
    Core = 0,
    Gpu = 1,
    Cache = 2,
    Uncore = 3,
    AnalogIO = 4,
};

constexpr int kPlaneCount = 5;

constexpr const char* kPlaneNames[kPlaneCount] = {
    "core", "gpu", "cache", "uncore", "analogio",
};

constexpr const char* kPlaneDisplayNames[kPlaneCount] = {
    "CPU Core", "Intel GPU", "CPU Cache", "System Agent", "Analog IO",
};

struct MsrAddresses {
    uint64_t voltageOffsets = 0x150;
    uint64_t units = 0x606;
    uint64_t powerLimits = 0x610;
    uint64_t temp = 0x1A2;
};

struct PowerLimit {
    bool shortEnabled = false;
    double shortPower = 0.0;
    double shortTime = 0.0;
    bool longEnabled = false;
    double longPower = 0.0;
    double longTime = 0.0;
    bool locked = false;
    uint64_t backupRest = 0;
};

class Error : public std::runtime_error {
  public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

struct MsrBatchOp {
    uint16_t cpu = 0;
    uint16_t isrdmsr = 0;
    int32_t err = 0;
    uint32_t msr = 0;
    uint64_t msrdata = 0;
    uint64_t wmask = 0;
};

class MsrBackend {
  public:
    virtual ~MsrBackend() = default;

    virtual uint64_t read(int cpu, uint32_t msr) = 0;

    virtual void write(int cpu, uint32_t msr, uint64_t value) = 0;

    virtual void batch(std::vector<MsrBatchOp>& ops) = 0;

    virtual std::string name() const = 0;

    virtual bool supportsBatch() const { return false; }

    virtual std::vector<int> cpus() = 0;
};

std::unique_ptr<MsrBackend> createMsrBackend(std::string* diag = nullptr);

std::unique_ptr<MsrBackend> createDaemonBackend(std::string* diag = nullptr);

uint32_t convertRoundedOffset(int x);
uint32_t convertOffset(double mV);
int unconvertRoundedOffset(uint32_t y);
double unconvertOffset(uint32_t y);
uint64_t packOffset(int planeIndex, uint32_t offset, bool write);
double unpackOffset(uint64_t msrResponse);

int16_t  encodeOffsetRaw(double mV);
double   decodeOffsetRaw(int16_t raw);
uint64_t packOffsetData(int16_t raw, int plane);

uint64_t packOffsetRead(int plane);
uint64_t packOffsetCommit();
double   unpackOffsetValue(uint64_t resp);

double toSeconds(uint64_t val, double unit);
uint64_t fromSeconds(double val, double unit);

struct Sample {
    std::array<double, kPlaneCount> planes{};
    bool planesOk = false;

    int tccOffset = 0;
    int tjMax = 0;
    bool tempOk = false;

    PowerLimit powerLimit;
    bool powerLimitOk = false;

    bool turboOn = false;
    bool turboOk = false;

    double packageTemp = 0;
    double coreMaxTemp = 0;
    bool tempSensorOk = false;
    double fanRpm = 0;
    bool fanOk = false;
    double freqMhz = 0;
    bool freqOk = false;
    double powerW = 0;
    bool powerOk = false;
};

class Controller {
  public:
    explicit Controller(MsrAddresses addrs = {});
    ~Controller();

    double readOffset(Plane plane);

    std::array<double, kPlaneCount> readAllOffsets(bool* ok = nullptr);

    std::optional<double> readDomainVoltage(int domain);

    std::optional<bool> readDomainWritable(int domain);

    std::optional<double> readIccMaxByDomain(int domain);
    bool writeIccMaxByDomain(int domain, double amps);

    std::optional<int> readCacheRatioByDomain(int domain);

    bool writeOffsetByDomain(int domain, double mV);

    bool unlockVoltage();

    bool zeroAllOffsets();

    void flushMailbox();

    void setOffset(Plane plane, double mV, bool force);

    void readTemperature(int* tccOffset, int* tjMax);
    void setTemperature(int targetC);

    PowerLimit readPowerLimit();
    void setPowerLimit(const PowerLimit& pl);

    std::optional<bool> readTurbo();
    void setTurbo(bool enabled);

    Sample sample();

    std::string backendName() const;

    struct CoreTopology {
        int pCores = 0;
        int eCores = 0;
        int pMaxKHz = 0;
        int eMaxKHz = 0;
        std::vector<int> pIndex;
        std::vector<int> eIndex;
        bool hybrid = false;
    };
    CoreTopology readCoreTopology();

    std::array<int, 8> readBiosTurboRatios();

    struct PlatformInfo {
        int maxNonTurboRatio = 0;
        int minRatio = 0;
        int maxRatio = 0;
        bool ratioLocked = false;
        bool programmableRatio = false;
    };
    PlatformInfo readPlatformInfo();

    struct HwpRequest {
        int minPerf = 0;
        int maxPerf = 0;
        int desired = 0;
        int epp = 0;
        bool ok = false;
    };
    HwpRequest readHwpRequest();

    struct RingRatio {
        int maxRing = 0;
        int minRing = 0;
        bool ok = false;
    };
    RingRatio readRingRatio();

    struct MiscFlags {
        bool tvb = false;
        bool tvbKnown = false;
        bool tvbLimiting = false;
        bool vmaxStress = false;
        bool ringDown = false;
        bool overclock = false;
        bool mvBoost = false;
        bool tvbCaps = false;
        bool avx2Ratio = false;
        int avx2Offset = 0;
        bool ok = false;
    };
    MiscFlags readMiscFlags();

    bool writeAvxOffset(int value);
    bool writeTvb(bool enabled);

    bool tvbWritable();
    bool writeRingDownBin(bool enabled);

    struct PowerLimits {
        double pl1W = 0;
        double pl2W = 0;
        double pl1WindowUs = 0;
        double pl2WindowUs = 0;
        double pl1MaxW = 0;
        bool ok = false;
    };

    struct TurboPower610 {
        double pl1W = 0;
        bool pl1Enable = false;
        int pl1Window = 0;
        double pl2W = 0;
        bool pl2Enable = false;
        int pl2Window = 0;
        bool ok = false;
    };
    TurboPower610 readTurboPower610();
    bool writeTurboPower610(const TurboPower610& p);

    struct ClockMod {
        int dutyCycle = 0;
        bool enabled = false;
        bool ok = false;
    };
    ClockMod readClockMod();
    bool writeClockMod(int dutyPercent);

    PowerLimits readPowerLimits();
    bool setPl1(double watts);
    bool setPl2(double watts);

    std::array<int, 8> readTurboRatios();
    void setTurboRatios(const std::array<int, 8>& ratios);

    void setTurboRatioP(int ratio);
    void setTurboRatioE(int ratio);

    void setSpeedShiftEpp(int epp);

    static int ratioToFreq(int ratio);
    static int freqToRatio(int targetFreq);

    void setPp0PowerLimit(double watts);
    void setRingPowerLimit(double watts);

    std::optional<double> readIccMax(Plane plane);
    int readFivrDomainCount();
    void setIccMax(Plane plane, double amps);

    std::optional<int> readCacheRatio();
    void setCacheRatio(int ratio);

    std::optional<int> readEpp();
    void setEpp(int epp);

    void setVfPoint(Plane plane, int profile, int pointIndex, int mV);
    std::optional<int> getVfPoint(Plane plane, int profile, int pointIndex);

    struct LimitReason {

        struct Group {
            bool bd_prochot = false;
            bool thermal    = false;
            bool residency  = false;
            bool avg_thermal= false;
            bool vr_thermal = false;
            bool vr_current = false;
            bool edp_other  = false;
            bool reserved9  = false;
            bool pl1        = false;
            bool pl2        = false;
            bool edp_current= false;
            bool bit13      = false;
            bool tvb        = false;
            bool vmax       = false;
            uint64_t raw = 0;
        };
        Group core;
        Group gpu;
        Group ring;
    };
    std::optional<LimitReason> readLimitReasons();
    uint64_t readLimitReasonsRaw();
    void clearLimitReasons();

    std::optional<bool> readTurboEnabled();
    void setTurboEnabled(bool on);
    std::optional<bool> readSpeedStep();
    void setSpeedStep(bool on);
    std::optional<bool> readBdProchot();
    void setBdProchot(bool on);

    std::optional<int> readEpb();

    std::optional<uint32_t> readMicrocodeRevision();
    bool fixEpb();

    struct CStateInfo {
        bool enabled = false;
        bool locked = false;
    };
    std::optional<CStateInfo> readCState();

    bool setCState(bool enabled);

    std::optional<bool> readSlfm();
    bool setSlfm(bool on);

    struct CStateConfig {
        bool cStateEnable = false;
        bool c1Demotion = false;
        bool c3Demotion = false;
        bool c1Undemotion = false;
        bool c3Undemotion = false;
        int pkgCStateLimit = 0;
        bool locked = false;
        bool ok = false;
    };
    CStateConfig readCStateConfig();
    bool writeCStateConfig(const CStateConfig& cfg);

    struct PerCoreSample {
        int cpu = 0;
        int fid = 0;
        double c0pct = 0;
        double modPct = 100.0;
        int tempC = 0;
        double freqMhz = 0;
        bool valid = false;
    };
    std::vector<PerCoreSample> samplePerCore();

    std::vector<int> readPerCoreRatios();
    std::vector<int> readPerCoreDefaults();

    struct FreqVid {
        double multiplier = 0;
        double vid = 0;
        bool hsFormat = false;
    };
    std::optional<FreqVid> readFreqVid();

    std::optional<double> readBclk();

    std::optional<int> readTemperatureC();

    struct RaplPower {
        double pkgW = 0;
        double dramW = 0;
        bool ok = false;
    };
    RaplPower sampleRaplPower();

    std::optional<double> sampleC0Percent();

  private:
    MsrAddresses addrs_;
    std::unique_ptr<MsrBackend> msr_;
    std::vector<int> cpus_;
    double lastRaplEnergy_ = 0;
    double lastDramEnergy_ = 0;
    std::chrono::steady_clock::time_point lastRaplTime_;
    bool raplPrimed_ = false;
};

std::optional<uint64_t> readRaplEnergy(int domain = 0);

std::optional<double> raplPowerDelta(uint64_t prevEnergy,
                                     std::chrono::steady_clock::time_point prevTime,
                                     uint64_t* outEnergy,
                                     std::chrono::steady_clock::time_point* outTime);

struct TempReading {
    double package = 0;
    double coreMax = 0;
    bool ok = false;
};
TempReading readHwmonTemps();

std::optional<double> readFanRpm();

std::optional<double> readCpuFreq();

std::string cpuModelName();
int cpuThreadCount();

bool isBatteryDischarging();

bool hasMsrAccess();

int runDaemon();

}
