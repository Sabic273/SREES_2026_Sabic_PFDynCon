#pragma once
#include <compiler/Definitions.h>
#include <sc/IPlugin.h>
#include <gui/LineEdit.h>
#include <cnt/PushBackVector.h>
#include <vector>
#include <functional>

#ifdef MU_WINDOWS
#ifdef PLUGIN_EXPORTS
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __declspec(dllimport)
#endif
#else
#ifdef PLUGIN_EXPORTS
#define PLUGIN_API __attribute__((visibility("default")))
#else
#define PLUGIN_API
#endif
#endif

struct BusInfo {
    int    busId = 0;
    double origPd = 0.0;
    double origQd = 0.0;
    bool   isGenerator = false;
};

struct TimePoint {
    float t = 0.0f;
    float Pd = 0.0f;
    float Qd = 0.0f;
};

struct BusLoadProfile {
    int                   busId = 0;
    std::vector<TimePoint> points;
};

// Dinamicki parametri generatora sa "has" flagovima - omogucava DJELIMICNI
// override (npr. korisnik zada samo H i D za neki generator, ostali
// parametri ostaju na default vrijednostima iz dynGenParameters()).
struct GenDynParams {
    int  busId = 0;

    bool hasH = false;     double H = 0.0;      // inercijska konstanta
    bool hasD = false;     double D = 0.0;      // damping
    bool hasXd = false;    double Xd = 0.0;     // sinhrona reaktansa d-osa
    bool hasXdp = false;   double Xdp = 0.0;    // prelazna reaktansa d-osa
    bool hasXq = false;    double Xq = 0.0;     // sinhrona reaktansa q-osa
    bool hasXqp = false;   double Xqp = 0.0;    // prelazna reaktansa q-osa
    bool hasTdop = false;  double Tdop = 0.0;   // Tdo'
    bool hasTqop = false;  double Tqop = 0.0;   // Tqo'
    bool hasTr = false;    double Tr = 0.0;     // vremenska konstanta mjernog kola AVR-a
    bool hasTgov = false;  double Tgov = 0.0;   // vremenska konstanta governora
    bool hasKa = false;    double Ka = 0.0;     // pojacanje AVR-a
    bool hasTa = false;    double Ta = 0.0;     // vremenska konstanta AVR-a
    bool hasKe = false;    double Ke = 0.0;     // pojacanje eksitera
    bool hasTe = false;    double Te = 0.0;     // vremenska konstanta eksitera
    bool hasSe = false;    double Se = 0.0;     // koeficijent zasicenja eksitera
    bool hasKf = false;    double Kf = 0.0;     // pojacanje stabilizacione povratne sprege
    bool hasTf = false;    double Tf = 0.0;     // vremenska konstanta stabilizacione povratne sprege
    bool hasR = false;     double R = 0.0;      // statizam governora (droop)
    bool hasRs = false;    double Rs = 0.0;     // otpor statora
};

struct Options {
    td::String modelName;
    td::INT4   maxIter = 10;
    float      dTime = 0.001f;
    float      endTime = 60.f;
    bool       showSpeeds = true;
    bool       showPowers = true;
    bool       showAngles = true;
    bool       showVoltages = true;
    cnt::PushBackVector<int>       selectedBuses;
    std::vector<BusLoadProfile>    loadProfiles;
    std::vector<GenDynParams>      genParams;
};

void onClosedPluginWindow();

bool getAllBusInfo(const td::String& fileName,
    cnt::PushBackVector<int>& genBuses,
    cnt::PushBackVector<BusInfo>& allBuses,
    gui::LineEdit& status);

// Parsira fajl poremecaja tereta. Fajl moze (opciono) zapoceti jednim ili
// vise "PLOT" redova koji navode koji se cvorovi prikazuju na graficima,
// PRIJE bilo kojeg BUS bloka, npr.:
//   PLOT 1 3 5-9 12
//   BUS 1
//   0.5 1.2 0.4
//   6.0 0.8 0.3
//   END
// "PLOT" red podrzava pojedinacne brojeve cvorova, opsege ("5-9") i/ili
// zarezom odvojene vrijednosti. outSelectedBuses ce ostati prazan ako
// fajl ne sadrzi nijedan PLOT red (u tom slucaju se podrazumijeva prikaz
// svih cvorova, kao i do sada).
bool parseLoadEventsFile(const td::String& fileName,
    const cnt::PushBackVector<BusInfo>& allBuses,
    std::vector<BusLoadProfile>& outProfiles,
    std::vector<int>& outSelectedBuses,
    gui::LineEdit& status);

// Parsira fajl sa dinamickim parametrima generatora. Format:
//   GEN <busId>
//   kljuc=vrijednost kljuc=vrijednost ...
//   ...
//   END
// Podrzani kljucevi: H D Xd Xdp Xq Xqp Tdop Tqop Tr Tgov Ka Ta Ke Te Se Kf Tf R Rs
// (jednaki nazivi parametara kao u dynGenParameters()). Moze ih biti vise na
// istom redu (razdvojenih razmakom) ili svaki u svom redu. Nije potrebno
// navesti sve kljuceve - nenavedeni parametri za taj generator ostaju na
// default vrijednostima. Validira se: postojanje cvora, da li je cvor
// stvarno generator, duplikati GEN blokova, nepoznati kljucevi i da su
// vremenske konstante (H, Tdop, Tqop, Ta, Te, Tf, Tgov) > 0.
bool parseGenParamsFile(const td::String& fileName,
    const cnt::PushBackVector<BusInfo>& allBuses,
    std::vector<GenDynParams>& outGenParams,
    gui::LineEdit& status);

// onProgress (opciono) se poziva sa vrijednoscu u [0,1] nakon svake vece
// faze generisanja modela - koristi se za GUI progress indicator kod
// velikih sistema (npr. case2736sp). Prazan std::function je bezbjedan
// (ne poziva se nista ako nije zadan).
bool createModel(const td::String& inputFileName,
    const td::String& outFileName,
    sc::IPlugin* pIPlugin,
    const Options& options,
    gui::LineEdit& status,
    const std::function<void(double)>& onProgress = std::function<void(double)>());