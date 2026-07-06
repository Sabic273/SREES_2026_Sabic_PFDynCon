#include "PowerFlowPlugin.h"
#include <sc/IPlugin.h>
#include "WindowPlugin.h"
#include <td/StringUtils.h>
#include <dense/Matrix.h>
#include <mu/ScopedCLocale.h>

class Plugin : public sc::IPlugin
{
    MemoryArchiveContainer _outArchives;
    WindowPlugin* _pWnd = nullptr;
public:
    Plugin()
    {
        //dont change this
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = nullptr;
    }

    void show(gui::Window* parentWnd, MemoryArchiveContainer& archives, td::UINT4 wndID, const sc::IPlugin::Cleaner& cleaner, const sc::IPlugin::CallBack& onComplete) override final
    {
        //dont change this
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = archives[i];

        if (_pWnd)
            _pWnd->setFocus();
        else
        {
            _pWnd = new WindowPlugin(parentWnd, this, onComplete, cleaner, wndID);
            _pWnd->open();
        }
    }

    td::String getMenuName() const override final
    {
        return "Power flow Final";
    }

    arch::MemoryOut* getArchive(sc::IPlugin::ArchType type) override final
    {
        //dont change this
        auto iType = size_t(type);
        if (iType >= getMaxSupportedArchiveParts())
            return nullptr;

        return _outArchives[size_t(type)];
    }

    MemoryArchiveContainer& getArchives() override final
    {
        //dont change this
        return _outArchives;
    }

    td::String getOutFileName() const override final
    {
        //dont change this
        assert(_pWnd);
        return _pWnd->getOutFileName();
    }

    size_t getMaxSupportedArchiveParts() const override final
    {
        return size_t(ArchType::NA); //don't change this
    }

    ModelType getModelType() const override final
    {
        //NOTE: adjust this to match your converter type
        return ModelType::DAE;
    }

    void onClosedPluginWindow()
    {
        //dont change this
        _pWnd = nullptr;
    }

};

static Plugin s_plugin;

void onClosedPluginWindow()
{
    s_plugin.onClosedPluginWindow();
}

//Plugin requires extern C
extern "C"
{

    PLUGIN_API sc::IPlugin* getPluginInterface()
    {
        return &s_plugin;
    }
}

enum class Format { Unknown = 0, Plain, Matlab };
struct Bus {
    int id;        // bus_i
    int type;      // 1=PQ, 2=PV, 3=Slack
    double Pd;     // aktivno opterećenje [MW]
    double Qd;     // reaktivno opterećenje [MVAr]
    double Vm;     // napon [pu]
    double Va;     // ugao [deg]
    double Bs;
    double baseKV;
};

struct Gen {
    int bus;       // bus na koji je priključen
    double Pg;     // aktivna snaga [MW]
    double Qg;     // reaktivna snaga [MVAr]
    double Vg;     // zadani napon [pu]
};

struct Branch {
    int fbus;      // od čvora
    int tbus;      // do čvora
    double r;      // otpornost [pu]
    double x;      // reaktansa [pu]
    double b;     // susceptansa [pu]
    double ratio;
};

// ============================================================================
// Pomocna funkcija: parsira jedan token sa liste cvorova za prikaz.
// Podrzava pojedinacan broj ("7"), opseg ("5-9") i zarezom odvojene
// vrijednosti unutar istog tokena ("1,3,5-9") - korisno ako fajl koristi
// zareze bez razmaka.
// ============================================================================
static void parsePlotBusToken(const std::string& token, std::vector<int>& out)
{
    std::stringstream ss(token);
    std::string sub;
    while (std::getline(ss, sub, ','))
    {
        if (sub.empty()) continue;
        size_t dash = sub.find('-');
        if (dash != std::string::npos && dash > 0)
        {
            int from = std::atoi(sub.substr(0, dash).c_str());
            int to = std::atoi(sub.substr(dash + 1).c_str());
            for (int v = from; v <= to; ++v) out.push_back(v);
        }
        else
        {
            int v = std::atoi(sub.c_str());
            if (v != 0) out.push_back(v);
        }
    }
}

bool parseLoadEventsFile(const td::String& fileName,
    const cnt::PushBackVector<BusInfo>& allBuses,
    std::vector<BusLoadProfile>& outProfiles,
    std::vector<int>& outSelectedBuses,
    gui::LineEdit& status)
{
    // Forsira "C" lokalu za vrijeme parsiranja - bez ovoga, na sistemima
    // gdje je regionalna lokala postavljena na decimalni zarez (npr.
    // bosanska/hrvatska/srpska lokala), std::atof("0.5") bi stao na tacku
    // i vratio 0.0 umjesto 0.5 (tacka se ne prepoznaje kao decimalni
    // separator u toj lokali).
    mu::ScopedCLocale scopedLocale;

    fo::InFile inFile;
    if (!fo::openExistingBinaryFile(inFile, fileName))
    {
        status = "ERROR! Ne mogu otvoriti fajl poremecaja!";
        return false;
    }

    auto busExists = [&](int busId) -> bool {
        for (size_t i = 0; i < allBuses.size(); ++i)
            if (allBuses[i].busId == busId) return true;
        return false;
        };

    outProfiles.reserve(8);
    outSelectedBuses.reserve(16);
    fo::LineNormal buffer;
    int  lineNum = 0;
    bool inBlock = false;
    bool seenBusBlock = false; // PLOT smije doci samo prije prvog BUS bloka
    BusLoadProfile currentProfile;

    while (fo::getLine(inFile, buffer))
    {
        ++lineNum;
        const char* pBuff = buffer.c_str();
        pBuff = td::findFirstNonWhiteSpace(pBuff);
        if (!pBuff || *pBuff == 0 || *pBuff == '#') continue;

        cnt::PushBackVector<td::String> tokens;
        tokens.reserve(4);
        td::String line(pBuff);
        line.split(" \t", tokens);
        if (tokens.size() == 0) continue;

        if (tokens[0].compareConstStr("PLOT") || tokens[0].compareConstStr("plot"))
        {
            if (inBlock)
            {
                status = "ERROR! PLOT red ne smije biti unutar BUS bloka!";
                return false;
            }
            if (seenBusBlock)
            {
                status = "ERROR! PLOT red mora biti prije svih BUS blokova!";
                return false;
            }
            for (size_t t = 1; t < tokens.size(); ++t)
                parsePlotBusToken(std::string(tokens[t].c_str()), outSelectedBuses);
            continue;
        }

        if (tokens[0].compareConstStr("BUS") || tokens[0].compareConstStr("bus"))
        {
            if (inBlock) { status = "ERROR! BUS bez prethodnog END!"; return false; }
            seenBusBlock = true;
            if (tokens.size() < 2)
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: BUS mora imati ID!", lineNum);
                status = td::String(err.c_str(), err.length());
                return false;
            }
            int busId = std::atoi(tokens[1].c_str());
            if (!busExists(busId))
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Cvor %d ne postoji u sistemu!", lineNum, busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }
            currentProfile.busId = busId;
            currentProfile.points.clear();
            inBlock = true;
            continue;
        }

        if (tokens[0].compareConstStr("END") || tokens[0].compareConstStr("end"))
        {
            if (!inBlock) { status = "ERROR! END bez prethodnog BUS!"; return false; }
            if (currentProfile.points.empty())
            {
                td::MutableString err;
                err.appendFormat("ERROR! Cvor %d nema ni jedan vremenski trenutak!", currentProfile.busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }

            std::sort(currentProfile.points.begin(), currentProfile.points.end(),
                [](const TimePoint& a, const TimePoint& b) { return a.t < b.t; });

            outProfiles.push_back(currentProfile);
            inBlock = false;
            continue;
        }

        if (!inBlock)
        {
            td::MutableString err;
            err.appendFormat("ERROR! Linija %d: Vremenski trenutak bez BUS bloka!", lineNum);
            status = td::String(err.c_str(), err.length());
            return false;
        }
        if (tokens.size() < 3)
        {
            td::MutableString err;
            err.appendFormat("ERROR! Linija %d: Ocekivano 't Pd Qd'", lineNum);
            status = td::String(err.c_str(), err.length());
            return false;
        }

        TimePoint pt;
        pt.t = (float)std::atof(tokens[0].c_str());
        pt.Pd = (float)std::atof(tokens[1].c_str());
        pt.Qd = (float)std::atof(tokens[2].c_str());

        if (pt.t < 0.0f)
        {
            td::MutableString err;
            err.appendFormat("ERROR! Linija %d: Vrijeme mora biti >= 0!", lineNum);
            status = td::String(err.c_str(), err.length());
            return false;
        }

        currentProfile.points.push_back(pt);
    }

    if (inBlock)
    {
        td::MutableString err;
        err.appendFormat("ERROR! Nedostaje END za cvor %d!", currentProfile.busId);
        status = td::String(err.c_str(), err.length());
        return false;
    }
    if (outProfiles.empty())
    {
        status = "ERROR! Fajl nema validnih unosa!";
        return false;
    }

    // Validacija PLOT cvorova - moraju postojati u sistemu
    for (size_t i = 0; i < outSelectedBuses.size(); ++i)
    {
        if (!busExists(outSelectedBuses[i]))
        {
            td::MutableString err;
            err.appendFormat("ERROR! PLOT: Cvor %d ne postoji u sistemu!", outSelectedBuses[i]);
            status = td::String(err.c_str(), err.length());
            return false;
        }
    }

    td::MutableString ok;
    if (outSelectedBuses.size() > 0)
        ok.appendFormat("OK! Ucitano %d profila tereta, %d cvorova za prikaz.",
            (int)outProfiles.size(), (int)outSelectedBuses.size());
    else
        ok.appendFormat("OK! Ucitano %d profila tereta. (PLOT nije naveden - prikazuju se svi cvorovi)",
            (int)outProfiles.size());
    status = td::String(ok.c_str(), ok.length());
    return true;
}
// ============================================================================
// Pomocna funkcija: parsira jedan "kljuc=vrijednost" token. Vraca false ako
// token nije u tom formatu ili ako vrijednost nije validan broj.
// ============================================================================
static bool parseKeyValueToken(const std::string& token, std::string& outKey, double& outValue)
{
    size_t eq = token.find('=');
    if (eq == std::string::npos || eq == 0 || eq == token.size() - 1)
        return false;

    outKey = token.substr(0, eq);
    std::string valStr = token.substr(eq + 1);

    char* endPtr = nullptr;
    outValue = std::strtod(valStr.c_str(), &endPtr);
    if (endPtr == valStr.c_str())
        return false; // nije parsiran nijedan broj

    return true;
}

bool parseGenParamsFile(const td::String& fileName,
    const cnt::PushBackVector<BusInfo>& allBuses,
    std::vector<GenDynParams>& outGenParams,
    gui::LineEdit& status)
{
    // Ista napomena kao u parseLoadEventsFile: std::strtod je locale-ovisan,
    // pa forsiramo "C" lokalu (decimalna tacka) za vrijeme parsiranja.
    mu::ScopedCLocale scopedLocale;

    fo::InFile inFile;
    if (!fo::openExistingBinaryFile(inFile, fileName))
    {
        status = "ERROR! Ne mogu otvoriti fajl sa parametrima generatora!";
        return false;
    }

    auto findBus = [&](int busId) -> const BusInfo* {
        for (size_t i = 0; i < allBuses.size(); ++i)
            if (allBuses[i].busId == busId) return &allBuses[i];
        return nullptr;
        };

    auto busAlreadyLoaded = [&](int busId) -> bool {
        for (size_t i = 0; i < outGenParams.size(); ++i)
            if (outGenParams[i].busId == busId) return true;
        return false;
        };

    outGenParams.reserve(8);
    fo::LineNormal buffer;
    int  lineNum = 0;
    bool inBlock = false;
    GenDynParams current;

    while (fo::getLine(inFile, buffer))
    {
        ++lineNum;
        const char* pBuff = buffer.c_str();
        pBuff = td::findFirstNonWhiteSpace(pBuff);
        if (!pBuff || *pBuff == 0 || *pBuff == '#') continue;

        cnt::PushBackVector<td::String> tokens;
        tokens.reserve(8);
        td::String line(pBuff);
        line.split(" \t", tokens);
        if (tokens.size() == 0) continue;

        if (tokens[0].compareConstStr("GEN") || tokens[0].compareConstStr("gen"))
        {
            if (inBlock)
            {
                status = "ERROR! GEN red bez prethodnog END!";
                return false;
            }
            if (tokens.size() < 2)
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: GEN mora imati ID cvora!", lineNum);
                status = td::String(err.c_str(), err.length());
                return false;
            }

            int busId = std::atoi(tokens[1].c_str());
            const BusInfo* pBus = findBus(busId);
            if (!pBus)
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Cvor %d ne postoji u sistemu!", lineNum, busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }
            if (!pBus->isGenerator)
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Cvor %d nije generator (nema GEN unos u Matpower fajlu)!", lineNum, busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }
            if (busAlreadyLoaded(busId))
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Duplikat GEN bloka za cvor %d!", lineNum, busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }

            current = GenDynParams();
            current.busId = busId;
            inBlock = true;
            continue;
        }

        if (tokens[0].compareConstStr("END") || tokens[0].compareConstStr("end"))
        {
            if (!inBlock)
            {
                status = "ERROR! END bez prethodnog GEN!";
                return false;
            }

            // Sanity provjera: vremenske konstante moraju biti > 0 (koriste se
            // kao djelilac u modelODE/modelNLE, pa 0 ili negativna vrijednost
            // vodi u dijeljenje sa 0 / nestabilan model).
            bool badTimeConst =
                (current.hasH && current.H <= 0.0) ||
                (current.hasTdop && current.Tdop <= 0.0) ||
                (current.hasTqop && current.Tqop <= 0.0) ||
                (current.hasTa && current.Ta <= 0.0) ||
                (current.hasTe && current.Te <= 0.0) ||
                (current.hasTf && current.Tf <= 0.0) ||
                (current.hasTgov && current.Tgov <= 0.0);

            if (badTimeConst)
            {
                td::MutableString err;
                err.appendFormat("ERROR! Cvor %d: H, Tdop, Tqop, Ta, Te, Tf i Tgov moraju biti > 0!", current.busId);
                status = td::String(err.c_str(), err.length());
                return false;
            }

            outGenParams.push_back(current);
            inBlock = false;
            continue;
        }

        if (!inBlock)
        {
            td::MutableString err;
            err.appendFormat("ERROR! Linija %d: Parametar (kljuc=vrijednost) bez prethodnog GEN bloka!", lineNum);
            status = td::String(err.c_str(), err.length());
            return false;
        }

        // Svaki token na liniji je oblika kljuc=vrijednost; moze ih biti vise
        // na istoj liniji (razdvojenih razmakom) ili svaki u svom redu.
        for (size_t t = 0; t < tokens.size(); ++t)
        {
            std::string key;
            double value = 0.0;
            if (!parseKeyValueToken(std::string(tokens[t].c_str()), key, value))
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Neispravan format '%s' (ocekivano kljuc=vrijednost)!",
                    lineNum, tokens[t].c_str());
                status = td::String(err.c_str(), err.length());
                return false;
            }

            if (key == "H") { current.hasH = true;    current.H = value; }
            else if (key == "D") { current.hasD = true;    current.D = value; }
            else if (key == "Xd") { current.hasXd = true;   current.Xd = value; }
            else if (key == "Xdp") { current.hasXdp = true;  current.Xdp = value; }
            else if (key == "Xq") { current.hasXq = true;   current.Xq = value; }
            else if (key == "Xqp") { current.hasXqp = true;  current.Xqp = value; }
            else if (key == "Tdop") { current.hasTdop = true; current.Tdop = value; }
            else if (key == "Tqop") { current.hasTqop = true; current.Tqop = value; }
            else if (key == "Tr") { current.hasTr = true;   current.Tr = value; }
            else if (key == "Tgov") { current.hasTgov = true; current.Tgov = value; }
            else if (key == "Ka") { current.hasKa = true;   current.Ka = value; }
            else if (key == "Ta") { current.hasTa = true;   current.Ta = value; }
            else if (key == "Ke") { current.hasKe = true;   current.Ke = value; }
            else if (key == "Te") { current.hasTe = true;   current.Te = value; }
            else if (key == "Se") { current.hasSe = true;   current.Se = value; }
            else if (key == "Kf") { current.hasKf = true;   current.Kf = value; }
            else if (key == "Tf") { current.hasTf = true;   current.Tf = value; }
            else if (key == "R") { current.hasR = true;    current.R = value; }
            else if (key == "Rs") { current.hasRs = true;   current.Rs = value; }
            else
            {
                td::MutableString err;
                err.appendFormat("ERROR! Linija %d: Nepoznat parametar '%s'!", lineNum, key.c_str());
                status = td::String(err.c_str(), err.length());
                return false;
            }
        }
    }

    if (inBlock)
    {
        td::MutableString err;
        err.appendFormat("ERROR! Nedostaje END za cvor %d!", current.busId);
        status = td::String(err.c_str(), err.length());
        return false;
    }
    if (outGenParams.empty())
    {
        status = "ERROR! Fajl nema validnih GEN blokova!";
        return false;
    }

    td::MutableString ok;
    ok.appendFormat("OK! Ucitano %d setova parametara generatora.", (int)outGenParams.size());
    status = td::String(ok.c_str(), ok.length());
    return true;
}

struct MatpowerData {
    double baseMVA;
    cnt::PushBackVector<Bus> buses;
    cnt::PushBackVector<Gen> gens;
    cnt::PushBackVector<Branch> branches;
};

// ============================================================================
// Pomocna funkcija: gradi Y-bus matricu iz podataka o granama.
// Ispravno akumulira paralelne grane jer sabira po paru (i,j), ne po grani.
// Koristi se na vise mjesta u kodu kako bi se izbjeglo dupliranje logike.
// ============================================================================
static void buildYbus(const MatpowerData& data,
    int maxBusId,
    dense::Matrix<double>& G,
    dense::Matrix<double>& B)
{
    G.reserve(maxBusId + 1, maxBusId + 1, nullptr, true);
    B.reserve(maxBusId + 1, maxBusId + 1, nullptr, true);
    auto Gm = G.getManipulator();
    auto Bm = B.getManipulator();

    for (size_t i = 0; i < data.branches.size(); ++i)
    {
        int f = data.branches[i].fbus;
        int t = data.branches[i].tbus;
        double r = data.branches[i].r;
        double x = data.branches[i].x;
        double b_shunt = data.branches[i].b;
        double k = (data.branches[i].ratio == 0.0) ? 1.0 : data.branches[i].ratio;

        double z_sq = r * r + x * x;
        if (z_sq == 0.0) continue;

        double g_line = r / z_sq;
        double b_line = -x / z_sq;

        // Vandermonde-ova formula za pi-model sa transformatorom
        // Svaka paralelna grana se automatski SUMIRA jer koristimo +=
        Gm(f, t) -= g_line / k;       Bm(f, t) -= b_line / k;
        Gm(t, f) -= g_line / k;       Bm(t, f) -= b_line / k;
        Gm(f, f) += g_line / (k * k); Bm(f, f) += (b_line + b_shunt / 2.0) / (k * k);
        Gm(t, t) += g_line;           Bm(t, t) += (b_line + b_shunt / 2.0);
    }

    // Shunt susceptanse sabirnica
    for (size_t i = 0; i < data.buses.size(); ++i)
    {
        int bId = data.buses[i].id;
        if (data.buses[i].Bs != 0.0)
            Bm(bId, bId) += data.buses[i].Bs / data.baseMVA;
    }
}

static bool parseMatpower(const td::String& fileName,
    MatpowerData& data,
    gui::LineEdit& status)
{
    fo::InFile inFile;
    if (!fo::openExistingBinaryFile(inFile, fileName))
    {
        status = "ERROR! Cannot open input file!";
        return false;
    }

    enum class ParseState { None, Bus, Gen, Branch };
    ParseState state = ParseState::None;

    bool shouldConvert = false;
    cnt::PushBackVector<td::String> tokens;
    tokens.reserve(32);

    fo::LineNormal buffer;

    while (fo::getLine(inFile, buffer))
    {
        const char* pBuff = buffer.c_str();
        pBuff = td::findFirstNonWhiteSpace(pBuff);
        if (!pBuff) continue;
        char ch = *pBuff;
        if (ch == 0) continue;

        const char* pComment = ::strchr(buffer.c_str(), '%');
        if (pComment != nullptr)
        {
            if (::strstr(pComment, "kW") != nullptr)
                shouldConvert = true;

            if (ch == '%') continue;
        }

        td::String str(pBuff);

        if (::strstr(str.c_str(), "mpc.baseMVA") != nullptr) {
            str.split('=', tokens);
            if (tokens.size() == 2)
            {
                td::String valStr = tokens[1];
                valStr.trim();
                const char* pVal = valStr.c_str();
                data.baseMVA = std::atof(pVal);
            }
            continue;
        }

        str.split(" \t;", tokens);
        if (tokens.size() == 0) continue;

        if (tokens[0].compareConstStr("mpc.bus")) { state = ParseState::Bus;    continue; }
        if (tokens[0].compareConstStr("mpc.gen")) { state = ParseState::Gen;    continue; }
        if (tokens[0].compareConstStr("mpc.branch")) { state = ParseState::Branch; continue; }
        if (tokens[0].compareConstStr("];")) { state = ParseState::None;   continue; }
        if (tokens[0].compareConstStr("mpc.gencost")) break;

        if (state == ParseState::None) continue;

        if (state == ParseState::Bus && tokens.size() >= 10)
        {
            Bus bus;
            bus.id = std::atoi(tokens[0].c_str());
            bus.type = std::atoi(tokens[1].c_str());
            bus.Pd = std::atof(tokens[2].c_str());
            bus.Qd = std::atof(tokens[3].c_str());
            bus.Bs = std::atof(tokens[5].c_str());
            bus.Vm = std::atof(tokens[7].c_str());
            bus.Va = std::atof(tokens[8].c_str()) * 3.141592653589793 / 180.0;
            bus.baseKV = std::atof(tokens[9].c_str());

            if (shouldConvert) {
                bus.Pd /= 1000.0;
                bus.Qd /= 1000.0;
            }

            data.buses.push_back(bus);
        }
        else if (state == ParseState::Gen && tokens.size() >= 6)
        {
            Gen gen;
            gen.bus = std::atoi(tokens[0].c_str());
            gen.Pg = std::atof(tokens[1].c_str());
            gen.Qg = std::atof(tokens[2].c_str());
            gen.Vg = std::atof(tokens[5].c_str());
            data.gens.push_back(gen);
        }
        else if (state == ParseState::Branch && tokens.size() >= 9)
        {
            Branch branch;
            branch.fbus = std::atoi(tokens[0].c_str());
            branch.tbus = std::atoi(tokens[1].c_str());
            branch.r = std::atof(tokens[2].c_str());
            branch.x = std::atof(tokens[3].c_str());
            branch.b = std::atof(tokens[4].c_str());
            branch.ratio = std::atof(tokens[8].c_str());
            data.branches.push_back(branch);
        }
    }

    if (data.buses.size() == 0 || data.gens.size() == 0 || data.branches.size() == 0)
    {
        status = "ERROR! Incomplete Matpower data maps!";
        return false;
    }

    // Sanacija baseKV = 0
    double slackBaseKV = data.buses[0].baseKV;
    for (auto& b : data.buses)
        if (b.baseKV <= 0.0)
            b.baseKV = slackBaseKV;

    auto getBusBaseKV = [&](int busId) -> double {
        for (const auto& b : data.buses)
            if (b.id == busId) return b.baseKV;
        return slackBaseKV;
        };

    for (auto& br : data.branches)
    {
        double vBaseFrom = getBusBaseKV(br.fbus);
        double vBaseTo = getBusBaseKV(br.tbus);

        if (shouldConvert && data.baseMVA <= 10.0)
        {
            double zBase = (vBaseFrom * vBaseFrom) / data.baseMVA;
            br.r /= zBase;
            br.x /= zBase;
        }

        if (br.ratio == 0.0 && std::abs(vBaseFrom - vBaseTo) > 0.1)
            br.ratio = vBaseFrom / vBaseTo;
    }

    std::ofstream out("C:/Users/Korisnik/OneDrive/Desktop/parsed_output.txt");
    if (out.is_open())
    {
        out << "baseMVA = " << data.baseMVA << "\n\n";

        out << "=== BUS DATA (" << data.buses.size() << " buseva) ===\n";
        out << "id\ttype\tPd[MW]\tQd[MVAr]\tBs\tVm[pu]\tVa[rad]\tbaseKV\n";
        for (const auto& b : data.buses)
            out << b.id << "\t" << b.type << "\t" << b.Pd << "\t"
            << b.Qd << "\t" << b.Bs << "\t" << b.Vm << "\t" << b.Va << "\t" << b.baseKV << "\n";

        out << "\n=== GEN DATA (" << data.gens.size() << " generatora) ===\n";
        out << "bus\tPg\tQg\tVg\n";
        for (const auto& g : data.gens)
            out << g.bus << "\t" << g.Pg << "\t" << g.Qg << "\t" << g.Vg << "\n";

        out << "\n=== BRANCH DATA (" << data.branches.size() << " vodova) ===\n";
        out << "fbus\ttbus\tr[pu]\tx[pu]\tb\tratio\n";
        for (const auto& br : data.branches)
            out << br.fbus << "\t" << br.tbus << "\t" << br.r << "\t"
            << br.x << "\t" << br.b << "\t" << br.ratio << "\n";

        out.close();
        std::cout << "OK! Podaci su uspješno normalizovani u p.u. Provjeri Desktop/parsed_output.txt" << std::endl;
    }

    return true;
}


void modelHeader(arch::MemoryOut& memDigitalOut, td::MutableString& mStr) {
    memDigitalOut.put("Header:\n");
    memDigitalOut.put("\tmaxIter = 100\n");
    memDigitalOut.put("\treport = Solver\n");
    memDigitalOut.put("\tmaxReps = -1\n");
    memDigitalOut.put("\toutToTxt = false\n");
    memDigitalOut.put("\ttxtFile = \"\"\n");
    memDigitalOut.put("\tstartTime = 0\n");
    memDigitalOut.put("\tdTime = 0.001\n");
    memDigitalOut.put("\tendTime = 12.000\n");
    memDigitalOut.put("end\n");

}

void modelVarsInit(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    const char* modelLine = "Model [type=DAE domain=real eps=1e-8 name=\"DAE:Dynamic-Model\" method=RK2 simplify=true pivot=Markowitz]:\n";
    const char* varsLine = "Vars [out=true]:\n";

    memDigitalOut.put(modelLine, strlen(modelLine));
    memDigitalOut.put(varsLine, strlen(varsLine));
    
    for (size_t i = 0; i < data.buses.size(); i++) {
        int bId = data.buses[i].id;
        double e_init = data.buses[i].Vm * cos(data.buses[i].Va);
        double f_init = data.buses[i].Vm * sin(data.buses[i].Va);

        mStr.reset();
        mStr.appendFormat("\te_%d = %.4f; f_%d = %.4f\n", bId, e_init, bId, f_init);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
    }
    mStr.reset();

    for (size_t i = 0; i < data.gens.size(); i++) {
        int k = data.gens[i].bus;
        mStr.appendFormat("\tVt_g%d = 1.0; ", k);
    }

    for (size_t i = 0; i < data.buses.size(); i++) {
        int bId = data.buses[i].id;
        bool isGen = false;
        for (size_t g = 0; g < data.gens.size(); g++)
            if (data.gens[g].bus == bId) { isGen = true; break; }
        if (!isGen)
            mStr.appendFormat("\tVt_%d = 1.0; ", bId);
    }

    mStr.append("\n");
    memDigitalOut.put(mStr.c_str(), mStr.length());
    
    mStr.reset();
}

void dynGenVarsInit(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, cnt::PushBackVector<Gen>& gens) {
    for (int i = 0; i < gens.size(); i++) {
        int k = gens[i].bus;

        mStr.reset();
        mStr.appendFormat("\t\xce\xb4_g%d; \xcf\x89_g%d = \xcf\x89_ref; Eq_p_g%d; Ed_p_g%d; Efd_g%d; Rf_g%d; VR_g%d; P_gmr_%d; P_gml_%d\n",
            k, k, k, k, k, k, k, k, k);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();

        mStr.appendFormat("\tId_g%d; Iq_g%d; Pe_g%d\n", k, k, k);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

void modelParams(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    int maxBusId = 0;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].id > maxBusId) maxBusId = data.buses[i].id;

    // Y-bus se gradi kroz zajednicku buildYbus() funkciju (dense::Matrix),
    // isto kao u submodelNLE/submodelPostProc/modelNLE - jedno mjesto za
    // logiku Y-bus akumulacije, umjesto duplirane implementacije.
    dense::Matrix<double> G, B;
    buildYbus(data, maxBusId, G, B);
    auto Gm = G.getManipulator();
    auto Bm = B.getManipulator();

    auto putLog = [&](const char* str) {
        memDigitalOut.put(str, strlen(str));
       
        };

    auto putMstr = [&]() {
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
        };

    putLog("Params:\n");

    double slackVm = 1.0, slackVa = 0.0;
    int slackId = 1;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].type == 3) {
            slackVm = data.buses[i].Vm;
            slackVa = data.buses[i].Va;
            slackId = data.buses[i].id;
            break;
        }

    double e_init = slackVm * cos(slackVa);
    double f_init = slackVm * sin(slackVa);
    mStr.appendFormat("\te_%d_init = %.4f [out=true]; f_%d_init = %.4f [out=true]\n",
        slackId, e_init, slackId, f_init);
    putMstr();

    putLog("\t// --- Y_bus elementi (G i B) ---\n");
    for (int i = 1; i <= maxBusId; ++i)
    {
        for (int j = 1; j <= maxBusId; ++j)
        {
            if (i == j || Gm(i, j) != 0.0 || Bm(i, j) != 0.0)
            {
                mStr.appendFormat("\tG_%d_%d = %.12f; B_%d_%d = %.12f\n",
                    i, j, Gm(i, j), i, j, Bm(i, j));
                putMstr();
            }
        }
    }

    putLog("\n\t// --- Injekcije snaga i zadani naponi ---\n");

    std::map<int, Gen> busToGen;
    for (size_t i = 0; i < data.gens.size(); ++i)
        busToGen[data.gens[i].bus] = data.gens[i];

    for (size_t i = 0; i < data.buses.size(); ++i)
    {
        int bId = data.buses[i].id;
        double base = data.baseMVA;
        double P_load_pu = data.buses[i].Pd / base;
        double Q_load_pu = data.buses[i].Qd / base;

        double Pg = 0.0, Qg = 0.0;
        bool hasGen = (busToGen.find(bId) != busToGen.end());
        if (hasGen) {
            Pg = busToGen[bId].Pg / base;
            Qg = busToGen[bId].Qg / base;
        }

        mStr.appendFormat("\tP_%d_g = %.4f; Q_%d_g = %.4f; P_%d_d = %.4f; Q_%d_d = %.4f; P_%d = P_%d_g - P_%d_d; Q_%d = Q_%d_g - Q_%d_d\n",
            bId, Pg, bId, Qg, bId, P_load_pu, bId, Q_load_pu, bId, bId, bId, bId, bId, bId);
        putMstr();

        if (data.buses[i].type == 2)
        {
            double Vsp = hasGen ? busToGen[bId].Vg : 1.0;
            mStr.appendFormat("\tV_%d_sp = %.4f\n", bId, Vsp);
            putMstr();
        }
    }

    for (size_t i = 0; i < data.gens.size(); ++i)
    {
        int k = data.gens[i].bus;
        mStr.appendFormat("\tTm_g%d; Vref_g%d\n", k, k);
        putMstr();
    }

    putLog("\tf_s = 50; \xcf\x89_ref = 1; \xcf\x89_s = 2*pi*f_s;\n\n");
}

void dynGenParameters(arch::MemoryOut& memDigitalOut, td::MutableString& mStr,
    cnt::PushBackVector<Gen>& gens, const std::vector<GenDynParams>& genParams) {

    // Lookup po busId za brz pristup custom parametrima (ako ih ima).
    std::map<int, const GenDynParams*> genParamsMap;
    for (size_t i = 0; i < genParams.size(); ++i)
        genParamsMap[genParams[i].busId] = &genParams[i];

    for (int i = 0; i < gens.size(); i++) {
        int k = gens[i].bus;

        // Default vrijednosti (iste kao prije - koriste se kad fajl sa
        // parametrima nije ucitan ili ne pokriva ovaj konkretan kljuc).
        double H = 6.4, D = 28;
        double Xd = 0.146, Xdp = 0.0608, Xq = 0.0969, Xqp = 0.0969;
        double Tdop = 8.96, Tqop = 0.31, Tr = 0.02, Tgov = 0.3;
        double Ka = 250.0, Ta = 0.02, Ke = 1.0, Te = 0.8;
        double R = 0.05, Rs = 0.07, Se = 0;
        double Kf = 0.063, Tf = 1;

        bool hasCustom = false;
        auto it = genParamsMap.find(k);
        if (it != genParamsMap.end())
        {
            const GenDynParams& p = *it->second;
            hasCustom = true;
            if (p.hasH)    H = p.H;
            if (p.hasD)    D = p.D;
            if (p.hasXd)   Xd = p.Xd;
            if (p.hasXdp)  Xdp = p.Xdp;
            if (p.hasXq)   Xq = p.Xq;
            if (p.hasXqp)  Xqp = p.Xqp;
            if (p.hasTdop) Tdop = p.Tdop;
            if (p.hasTqop) Tqop = p.Tqop;
            if (p.hasTr)   Tr = p.Tr;
            if (p.hasTgov) Tgov = p.Tgov;
            if (p.hasKa)   Ka = p.Ka;
            if (p.hasTa)   Ta = p.Ta;
            if (p.hasKe)   Ke = p.Ke;
            if (p.hasTe)   Te = p.Te;
            if (p.hasSe)   Se = p.Se;
            if (p.hasKf)   Kf = p.Kf;
            if (p.hasTf)   Tf = p.Tf;
            if (p.hasR)    R = p.R;
            if (p.hasRs)   Rs = p.Rs;
        }

        mStr.reset();
        mStr.appendFormat("\t// Dynamic parameters for gen %d%s\n", k, hasCustom ? " (custom - iz fajla)" : "");
        mStr.appendFormat("\tH_g%d = %.6g; D_g%d = %.6g\n", k, H, k, D);
        mStr.appendFormat("\tXd_g%d = %.6g; Xd_p_g%d = %.6g; Xq_g%d = %.6g; Xq_p_g%d = %.6g\n", k, Xd, k, Xdp, k, Xq, k, Xqp);
        mStr.appendFormat("\tTdo_p_g%d = %.6g; Tqo_p_g%d = %.6g; Tr_g%d = %.6g; T_gov_g%d = %.6g\n", k, Tdop, k, Tqop, k, Tr, k, Tgov);
        mStr.appendFormat("\tKa_g%d = %.6g; Ta_g%d = %.6g; Ke_g%d = %.6g; Te_g%d = %.6g\n", k, Ka, k, Ta, k, Ke, k, Te);
        mStr.appendFormat("\tR_g%d = %.6g; Rs_g%d = %.6g; Se_g%d = %.6g\n", k, R, k, Rs, k, Se);
        mStr.appendFormat("\tKf_g%d = %.6g; Tf_g%d = %.6g\n\n", k, Kf, k, Tf);

        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

void submodelVarsInit(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    const char* subModelLine = "SubModel [type=NL name=\"Initialization\" copyPars=-1 eps=1e-8 pivot=\"Diagonal\"]:\n";
    const char* varsLine = "Vars [out=true]:\n";

    memDigitalOut.put(subModelLine, strlen(subModelLine));
    memDigitalOut.put(varsLine, strlen(varsLine));
    

    int slackId = 1;
    for (size_t i = 0; i < data.buses.size(); ++i) {
        if (data.buses[i].type == 3) {
            slackId = data.buses[i].id;
            break;
        }
    }

    for (size_t i = 0; i < data.buses.size(); i++) {
        int bId = data.buses[i].id;

        if (bId == slackId) {
            mStr.reset();
            mStr.appendFormat("\t// Čvor %d je SLACK, izostavljen iz varijabli podmodela\n", bId);
            memDigitalOut.put(mStr.c_str(), mStr.length());
            
            mStr.reset();
            continue;
        }

        double e_init = data.buses[i].Vm * cos(data.buses[i].Va);
        double f_init = data.buses[i].Vm * sin(data.buses[i].Va);

        mStr.reset();
        mStr.appendFormat("\te_%d = %.4f; f_%d = %.4f\n", bId, e_init, bId, f_init);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

void submodelParamsInit(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, cnt::PushBackVector<Gen>& gens) {
    const char* paramsHeader = "Params:\n";
    memDigitalOut.put(paramsHeader, strlen(paramsHeader));
    

    for (int i = 0; i < gens.size(); i++) {
        int k = gens[i].bus;

        mStr.reset();
        mStr.appendFormat("\t// Gen %d initials\n", k);
        mStr.appendFormat("\tP_%d_init; Q_%d_init\n", k, k);
        mStr.appendFormat("\tI_r%d_init; I_i%d_init; Eq_r%d_init; Eq_i%d_init\n", k, k, k, k);
        mStr.appendFormat("\tI_d%d_init; I_q%d_init; V_d%d_init; V_q%d_init\n", k, k, k, k);
        mStr.appendFormat("\tE_d%d_p_init; E_q%d_p_init; Tm%d_init; Vt_g%d_init\n", k, k, k, k);
        mStr.appendFormat("\tEf_d%d_init; VR%d_init; Rf%d_init; Vref%d_init\n\n", k, k, k, k);

        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

void submodelNLE(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    const char* nlesHeader = "NLEs:\n";
    memDigitalOut.put(nlesHeader, strlen(nlesHeader));
    

    int slackId = 1;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].type == 3) { slackId = data.buses[i].id; break; }

    int maxBusId = 0;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].id > maxBusId) maxBusId = data.buses[i].id;

    dense::Matrix<double> G, B;
    buildYbus(data, maxBusId, G, B);
    auto Gm = G.getManipulator();
    auto Bm = B.getManipulator();

    for (size_t i = 0; i < data.buses.size(); ++i)
    {
        int bId = data.buses[i].id;
        int type = data.buses[i].type;

        if (type == 3)
        {
            mStr.reset();
            mStr.appendFormat("\t// node %d - SLACK (Fiksne vrijednosti, preskace se)\n", bId);
            memDigitalOut.put(mStr.c_str(), mStr.length());
            
            mStr.reset();
            continue;
        }

        td::MutableString sumRe, sumIm;
        sumRe.appendFormat("G_%d_%d*e_%d - B_%d_%d*f_%d", bId, bId, bId, bId, bId, bId);
        sumIm.appendFormat("G_%d_%d*f_%d + B_%d_%d*e_%d", bId, bId, bId, bId, bId, bId);

        // Iteracija po ID-ovima sabirnica (ne po granama) — ispravno za paralelne grane
        for (int j = 1; j <= maxBusId; ++j)
        {
            if (j == bId) continue;
            if (Gm(bId, j) == 0.0 && Bm(bId, j) == 0.0) continue;

            if (j == slackId) {
                sumRe.appendFormat(" + G_%d_%d*e_%d_init - B_%d_%d*f_%d_init", bId, j, j, bId, j, j);
                sumIm.appendFormat(" + G_%d_%d*f_%d_init + B_%d_%d*e_%d_init", bId, j, j, bId, j, j);
            }
            else {
                sumRe.appendFormat(" + G_%d_%d*e_%d - B_%d_%d*f_%d", bId, j, j, bId, j, j);
                sumIm.appendFormat(" + G_%d_%d*f_%d + B_%d_%d*e_%d", bId, j, j, bId, j, j);
            }
        }

        mStr.reset();

        if (type == 2)
        {
            mStr.appendFormat("\t// node %d - PV\n", bId);
            mStr.appendFormat("\te_%d*(%s) + f_%d*(%s) = P_%d\n",
                bId, sumRe.c_str(), bId, sumIm.c_str(), bId);
            mStr.appendFormat("\te_%d^2 + f_%d^2 = V_%d_sp^2\n", bId, bId, bId);
        }
        else if (type == 1)
        {
            bool isZI = (data.buses[i].Pd == 0.0 && data.buses[i].Qd == 0.0);
            if (isZI)
            {
                mStr.appendFormat("\t// node %d - ZI\n", bId);
                mStr.appendFormat("\t%s = 0\n", sumRe.c_str());
                mStr.appendFormat("\t%s = 0\n", sumIm.c_str());
            }
            else
            {
                mStr.appendFormat("\t// node %d - PQ\n", bId);
                mStr.appendFormat("\te_%d*(%s) + f_%d*(%s) = P_%d\n",
                    bId, sumRe.c_str(), bId, sumIm.c_str(), bId);
                mStr.appendFormat("\tf_%d*(%s) - e_%d*(%s) = Q_%d\n",
                    bId, sumRe.c_str(), bId, sumIm.c_str(), bId);
            }
        }

        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

void submodelPostProc(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    const char* postProcHeader = "PostProc:\n";
    memDigitalOut.put(postProcHeader, strlen(postProcHeader));
    

    int slackId = 1;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].type == 3) { slackId = data.buses[i].id; break; }

    std::map<int, size_t> busIdToIndex;
    for (size_t i = 0; i < data.buses.size(); ++i)
        busIdToIndex[data.buses[i].id] = i;

    int maxBusId = 0;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].id > maxBusId) maxBusId = data.buses[i].id;

    dense::Matrix<double> G, B;
    buildYbus(data, maxBusId, G, B);
    auto Gm = G.getManipulator();
    auto Bm = B.getManipulator();

    auto putMstr = [&]() {
        memDigitalOut.put(mStr.c_str(), mStr.length());
       
        mStr.reset();
        };

    // P_init i Q_init za slack
    mStr.appendFormat("\tP_%d_init = e_%d_init*(G_%d_%d*e_%d_init - B_%d_%d*f_%d_init",
        slackId, slackId, slackId, slackId, slackId, slackId, slackId, slackId);
    for (int j = 1; j <= maxBusId; ++j) {
        if (j == slackId) continue;
        if (Gm(slackId, j) == 0.0 && Bm(slackId, j) == 0.0) continue;
        mStr.appendFormat(" + G_%d_%d*e_%d - B_%d_%d*f_%d", slackId, j, j, slackId, j, j);
    }
    mStr.appendFormat(") + f_%d_init*(G_%d_%d*f_%d_init + B_%d_%d*e_%d_init",
        slackId, slackId, slackId, slackId, slackId, slackId, slackId);
    for (int j = 1; j <= maxBusId; ++j) {
        if (j == slackId) continue;
        if (Gm(slackId, j) == 0.0 && Bm(slackId, j) == 0.0) continue;
        mStr.appendFormat(" + G_%d_%d*f_%d + B_%d_%d*e_%d", slackId, j, j, slackId, j, j);
    }
    mStr.append(")\n");
    putMstr();

    mStr.appendFormat("\tQ_%d_init = f_%d_init*(G_%d_%d*e_%d_init - B_%d_%d*f_%d_init",
        slackId, slackId, slackId, slackId, slackId, slackId, slackId, slackId);
    for (int j = 1; j <= maxBusId; ++j) {
        if (j == slackId) continue;
        if (Gm(slackId, j) == 0.0 && Bm(slackId, j) == 0.0) continue;
        mStr.appendFormat(" + G_%d_%d*e_%d - B_%d_%d*f_%d", slackId, j, j, slackId, j, j);
    }
    mStr.appendFormat(") - e_%d_init*(G_%d_%d*f_%d_init + B_%d_%d*e_%d_init",
        slackId, slackId, slackId, slackId, slackId, slackId, slackId);
    for (int j = 1; j <= maxBusId; ++j) {
        if (j == slackId) continue;
        if (Gm(slackId, j) == 0.0 && Bm(slackId, j) == 0.0) continue;
        mStr.appendFormat(" + G_%d_%d*f_%d + B_%d_%d*e_%d", slackId, j, j, slackId, j, j);
    }
    mStr.append(")\n\n");
    putMstr();

    // Petlja kroz generatore
    for (size_t i = 0; i < data.gens.size(); ++i)
    {
        int k = data.gens[i].bus;
        size_t bIdx = busIdToIndex[k];

        mStr.appendFormat("\t// --- Inicijalizacija Generatora na sabirnici %d ---\n", k);

        if (k == slackId) {
            mStr.appendFormat("\tP_%d_init = P_%d_init\n\tQ_%d_init = Q_%d_init\n", k, k, k, k);
            mStr.appendFormat("\tI_r%d_init = (P_%d_init*e_%d_init + Q_%d_init*f_%d_init) / (e_%d_init^2 + f_%d_init^2)\n", k, k, k, k, k, k, k);
            mStr.appendFormat("\tI_i%d_init = (P_%d_init*f_%d_init - Q_%d_init*e_%d_init) / (e_%d_init^2 + f_%d_init^2)\n", k, k, k, k, k, k, k);
            mStr.appendFormat("\tEq_r%d_init = e_%d_init + Rs_g%d*I_r%d_init - Xq_g%d*I_i%d_init\n", k, k, k, k, k, k);
            mStr.appendFormat("\tEq_i%d_init = f_%d_init + Rs_g%d*I_i%d_init + Xq_g%d*I_r%d_init\n", k, k, k, k, k, k);
        }
        else {
            mStr.appendFormat("\tP_%d_init = P_%d_g\n", k, k);

            double Q_load_pu = data.buses[bIdx].Qd / data.baseMVA;

            mStr.appendFormat("\tQ_%d_init = f_%d*(G_%d_%d*e_%d - B_%d_%d*f_%d", k, k, k, k, k, k, k, k);
            for (int j = 1; j <= maxBusId; ++j) {
                if (j == k) continue;
                if (Gm(k, j) == 0.0 && Bm(k, j) == 0.0) continue;
                if (j == slackId)
                    mStr.appendFormat(" + G_%d_%d*e_%d_init - B_%d_%d*f_%d_init", k, j, j, k, j, j);
                else
                    mStr.appendFormat(" + G_%d_%d*e_%d - B_%d_%d*f_%d", k, j, j, k, j, j);
            }
            mStr.appendFormat(") - e_%d*(G_%d_%d*f_%d + B_%d_%d*e_%d", k, k, k, k, k, k, k);
            for (int j = 1; j <= maxBusId; ++j) {
                if (j == k) continue;
                if (Gm(k, j) == 0.0 && Bm(k, j) == 0.0) continue;
                if (j == slackId)
                    mStr.appendFormat(" + G_%d_%d*f_%d_init + B_%d_%d*e_%d_init", k, j, j, k, j, j);
                else
                    mStr.appendFormat(" + G_%d_%d*f_%d + B_%d_%d*e_%d", k, j, j, k, j, j);
            }
            if (Q_load_pu != 0.0)
                mStr.appendFormat(") + Q_%d_d\n", k);
            else
                mStr.append(")\n");

            mStr.appendFormat("\tI_r%d_init = (P_%d_init*e_%d + Q_%d_init*f_%d) / (e_%d^2 + f_%d^2)\n", k, k, k, k, k, k, k);
            mStr.appendFormat("\tI_i%d_init = (P_%d_init*f_%d - Q_%d_init*e_%d) / (e_%d^2 + f_%d^2)\n", k, k, k, k, k, k, k);
            mStr.appendFormat("\tEq_r%d_init = e_%d + Rs_g%d*I_r%d_init - Xq_g%d*I_i%d_init\n", k, k, k, k, k, k);
            mStr.appendFormat("\tEq_i%d_init = f_%d + Rs_g%d*I_i%d_init + Xq_g%d*I_r%d_init\n", k, k, k, k, k, k);
        }

        mStr.appendFormat("\t@main.\xce\xb4_g%d = atg2(Eq_r%d_init, Eq_i%d_init)\n", k, k, k);
        mStr.appendFormat("\tI_d%d_init = I_r%d_init*sin(@main.\xce\xb4_g%d) - I_i%d_init*cos(@main.\xce\xb4_g%d)\n", k, k, k, k, k);
        mStr.appendFormat("\tI_q%d_init = I_r%d_init*cos(@main.\xce\xb4_g%d) + I_i%d_init*sin(@main.\xce\xb4_g%d)\n", k, k, k, k, k);

        if (k == slackId) {
            mStr.appendFormat("\tV_d%d_init = e_%d_init*sin(@main.\xce\xb4_g%d) - f_%d_init*cos(@main.\xce\xb4_g%d)\n", k, k, k, k, k);
            mStr.appendFormat("\tV_q%d_init = e_%d_init*cos(@main.\xce\xb4_g%d) + f_%d_init*sin(@main.\xce\xb4_g%d)\n", k, k, k, k, k);
        }
        else {
            mStr.appendFormat("\tV_d%d_init = e_%d*sin(@main.\xce\xb4_g%d) - f_%d*cos(@main.\xce\xb4_g%d)\n", k, k, k, k, k);
            mStr.appendFormat("\tV_q%d_init = e_%d*cos(@main.\xce\xb4_g%d) + f_%d*sin(@main.\xce\xb4_g%d)\n", k, k, k, k, k);
        }

        mStr.appendFormat("\tE_d%d_p_init = (Xq_g%d - Xq_p_g%d) * I_q%d_init\n", k, k, k, k);
        mStr.appendFormat("\tE_q%d_p_init = V_q%d_init + Rs_g%d * I_q%d_init + Xd_p_g%d * I_d%d_init\n", k, k, k, k, k, k);
        mStr.appendFormat("\tEf_d%d_init = E_q%d_p_init + (Xd_g%d - Xd_p_g%d)*I_d%d_init\n", k, k, k, k, k);
        mStr.appendFormat("\tVR%d_init = (Ke_g%d + Se_g%d) * Ef_d%d_init\n", k, k, k, k);
        mStr.appendFormat("\tRf%d_init = (Kf_g%d / Tf_g%d) * Ef_d%d_init\n", k, k, k, k);

        if (k == slackId)
            mStr.appendFormat("\tVt_g%d_init = sqrt(e_%d_init^2 + f_%d_init^2)\n", k, k, k);
        else
            mStr.appendFormat("\tVt_g%d_init = sqrt(e_%d^2 + f_%d^2)\n", k, k, k);

        mStr.appendFormat("\tVref%d_init = Vt_g%d_init + (VR%d_init / Ka_g%d)\n", k, k, k, k);
        mStr.appendFormat("\t@main.\xcf\x89_g%d = \xcf\x89_ref\n", k);
        mStr.appendFormat("\t@main.Ed_p_g%d = E_d%d_p_init\n", k, k);
        mStr.appendFormat("\t@main.Eq_p_g%d = E_q%d_p_init\n", k, k);
        mStr.appendFormat("\t@main.VR_g%d = VR%d_init\n", k, k);
        mStr.appendFormat("\t@main.Rf_g%d = Rf%d_init\n", k, k);
        mStr.appendFormat("\t@main.Id_g%d = I_d%d_init\n", k, k);
        mStr.appendFormat("\t@main.Iq_g%d = I_q%d_init\n", k, k);
        mStr.appendFormat("\t@main.Efd_g%d = Ef_d%d_init\n", k, k);
        mStr.appendFormat("\t@main.Vref_g%d = Vref%d_init\n", k, k);
        mStr.appendFormat("\tTm%d_init = @main.Ed_p_g%d*I_d%d_init + @main.Eq_p_g%d*I_q%d_init + (Xq_p_g%d - Xd_p_g%d)*I_d%d_init*I_q%d_init;\n",
            k, k, k, k, k, k, k, k, k);
        mStr.appendFormat("\t@main.Pe_g%d = Tm%d_init;\n", k, k);
        mStr.appendFormat("\t@main.P_gmr_%d = Tm%d_init;\n", k, k);
        mStr.appendFormat("\t@main.P_gml_%d = Tm%d_init;\n", k, k);
        mStr.appendFormat("\t@main.Tm_g%d = Tm%d_init;\n", k, k);

        if (k == slackId)
            mStr.appendFormat("\t@main.Q_%d = Q_%d_init\n\t@main.P_%d = P_%d_init\n\n", k, k, k, k);
        else
            mStr.appendFormat("\t@main.Q_%d_g = Q_%d_init\n\t@main.P_%d_g = P_%d_init\n\n", k, k, k, k);

        putMstr();
    }

    // Preslikavanje na @main
    mStr.append("\t// --- Preslikavanje stacionarnih velicina na glavne varijable ---\n");
    for (size_t i = 0; i < data.buses.size(); ++i)
    {
        int bId = data.buses[i].id;
        if (data.buses[i].type == 3)
            mStr.appendFormat("\t@main.e_%d = e_%d_init; @main.f_%d = f_%d_init\n", bId, bId, bId, bId);
        else
            mStr.appendFormat("\t@main.e_%d = e_%d; @main.f_%d = f_%d\n", bId, bId, bId, bId);
    }

    mStr.append("end\n");
    putMstr();
}

void modelODE(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, cnt::PushBackVector<Gen>& gens) {
    const char* odesHeader = "ODEs:\n";
    memDigitalOut.put(odesHeader, strlen(odesHeader));
    

    for (int i = 0; i < gens.size(); i++) {
        int k = gens[i].bus;

        mStr.reset();
        mStr.appendFormat("\t// --- G%d ---\n", k);
        mStr.appendFormat("\tEq_p_g%d' = (-Eq_p_g%d - (Xd_g%d - Xd_p_g%d)*Id_g%d + Efd_g%d)/Tdo_p_g%d\n", k, k, k, k, k, k, k);
        mStr.appendFormat("\tEd_p_g%d' = (-Ed_p_g%d + (Xq_g%d - Xq_p_g%d)*Iq_g%d)/Tqo_p_g%d\n", k, k, k, k, k, k);
        mStr.appendFormat("\t\xce\xb4_g%d'    = (\xcf\x89_g%d - \xcf\x89_ref) * \xcf\x89_s\n", k, k);
        mStr.appendFormat("\t\xcf\x89_g%d'    = (P_gml_%d - (Ed_p_g%d * Id_g%d + Eq_p_g%d * Iq_g%d + (Xq_p_g%d - Xd_p_g%d) * Id_g%d * Iq_g%d) - D_g%d*(\xcf\x89_g%d - \xcf\x89_ref)) * \xcf\x89_s /(2*H_g%d)\n",
            k, k, k, k, k, k, k, k, k, k, k, k, k, k);
        mStr.appendFormat("\tEfd_g%d'  = (-(Ke_g%d + Se_g%d)*Efd_g%d + VR_g%d)/Te_g%d\n", k, k, k, k, k, k);
        mStr.appendFormat("\tRf_g%d'   = (-Rf_g%d + (Kf_g%d/Tf_g%d)*Efd_g%d)/Tf_g%d\n", k, k, k, k, k, k);
        mStr.appendFormat("\tVR_g%d'   = (-VR_g%d + Ka_g%d*Rf_g%d - (Ka_g%d*Kf_g%d/Tf_g%d)*Efd_g%d + Ka_g%d*(Vref_g%d - sqrt(e_%d^2 + f_%d^2)))/Ta_g%d\n",
            k, k, k, k, k, k, k, k, k, k, k, k);
        mStr.appendFormat("\tP_gmr_%d' = (\xcf\x89_ref - \xcf\x89_g%d) / (T_gov_g%d * R_g%d * \xcf\x89_ref)\n\n", k, k, k, k);

        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

// ============================================================================
// modelNLE — POPRAVKA: Iteracija po ID-ovima sabirnica (ne po granama)
//
// STARI KOD je iterirao po data.branches i za svaku granu dodavao sumRe/sumIm
// termin za susjednu sabirnicu. Kada postoje paralelne grane (isti fbus-tbus
// par se pojavljuje vise puta), isti sused bi bio dodat VISE PUTA, iako
// parametar G_i_j vec sadrzi KUMULATIVNU admitansu svih paralelnih grana.
// To je uzrokovalo pogresne jednacine i pad simulacije.
//
// NOVI KOD gradi lokalni Y-bus (koji pravilno akumulira paralelne grane) i
// zatim iterira po j = 1..maxBusId — svaki par (i,j) se pojavljuje tacno
// jednom, bez obzira koliko ima paralelnih grana.
// ============================================================================
void modelNLE(arch::MemoryOut& memDigitalOut, td::MutableString& mStr, MatpowerData& data) {
    const char* nlesHeader = "NLEs:\n";
    memDigitalOut.put(nlesHeader, strlen(nlesHeader));
   

    std::map<int, Gen> busToGenMap;
    for (size_t i = 0; i < data.gens.size(); ++i)
        busToGenMap[data.gens[i].bus] = data.gens[i];

    // Gradimo Y-bus jednom — ispravno akumulira sve paralelne grane
    int maxBusId = 0;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].id > maxBusId) maxBusId = data.buses[i].id;

    dense::Matrix<double> G, B;
    buildYbus(data, maxBusId, G, B);
    auto Gm = G.getManipulator();
    auto Bm = B.getManipulator();

    int slackId = 1;
    for (size_t i = 0; i < data.buses.size(); ++i)
        if (data.buses[i].type == 3) { slackId = data.buses[i].id; break; }

    // --- Statorske jednacine generatora ---
    const char* statorComment = "\t// Stator algebraic equations\n";
    memDigitalOut.put(statorComment, strlen(statorComment));
    

    for (size_t i = 0; i < data.gens.size(); ++i)
    {
        int k = data.gens[i].bus;
        mStr.reset();
        mStr.appendFormat("\tId_g%d = (Rs_g%d * (Ed_p_g%d - (e_%d*sin(\xce\xb4_g%d) - f_%d*cos(\xce\xb4_g%d))) + Xq_p_g%d * (Eq_p_g%d - (e_%d*cos(\xce\xb4_g%d) + f_%d*sin(\xce\xb4_g%d)))) / (Rs_g%d^2 + Xd_p_g%d * Xq_p_g%d)\n",
            k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k);
        mStr.appendFormat("\tIq_g%d = (-Xd_p_g%d * (Ed_p_g%d - (e_%d*sin(\xce\xb4_g%d) - f_%d*cos(\xce\xb4_g%d))) + Rs_g%d * (Eq_p_g%d - (e_%d*cos(\xce\xb4_g%d) + f_%d*sin(\xce\xb4_g%d)))) / (Rs_g%d^2 + Xd_p_g%d * Xq_p_g%d)\n",
            k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k);
        mStr.appendFormat("\tP_gml_%d = lim(P_gmr_%d, 0, 10.0)\n", k, k);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }

    const char* newlineStr = "\n";
    memDigitalOut.put(newlineStr, strlen(newlineStr));
   
    // --- Elektricna snaga ---
    const char* elecPowerComment = "\t// Elec. power\n";
    memDigitalOut.put(elecPowerComment, strlen(elecPowerComment));
   
    for (size_t i = 0; i < data.gens.size(); ++i)
    {
        int k = data.gens[i].bus;
        mStr.reset();
        mStr.appendFormat("\tPe_g%d = Ed_p_g%d * Id_g%d + Eq_p_g%d * Iq_g%d + (Xq_p_g%d - Xd_p_g%d) * Id_g%d * Iq_g%d\n",
            k, k, k, k, k, k, k, k, k);
        memDigitalOut.put(mStr.c_str(), mStr.length());
       
        mStr.reset();
    }

    memDigitalOut.put(newlineStr, strlen(newlineStr));
    

    // --- Vt varijable ---
    const char* vtComment = "\t// Terminal voltages\n";
    memDigitalOut.put(vtComment, strlen(vtComment));
   
    for (size_t i = 0; i < data.gens.size(); ++i)
    {
        int k = data.gens[i].bus;
        mStr.reset();
        mStr.appendFormat("\tVt_g%d = sqrt(e_%d^2 + f_%d^2)\n", k, k, k);
        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
    for (const auto& b : data.buses)
    {
        bool isGen = false;
        for (const auto& g : data.gens)
            if (g.bus == b.id) { isGen = true; break; }

        if (!isGen)
        {
            std::string line = "\tVt_" + std::to_string(b.id) +
                " = sqrt(e_" + std::to_string(b.id) +
                "^2 + f_" + std::to_string(b.id) + "^2)\n";
            memDigitalOut.put(line.c_str(), line.length());
            
        }
    }
    memDigitalOut.put(newlineStr, strlen(newlineStr));
    

    // --- Mrezne jednacine ---
    const char* pfComment = "\t// Power Flow balance equations for all nodes\n";
    memDigitalOut.put(pfComment, strlen(pfComment));
   
    for (size_t i = 0; i < data.buses.size(); ++i)
    {
        int bId = data.buses[i].id;
        bool imaGen = (busToGenMap.find(bId) != busToGenMap.end());

        mStr.reset();

        // ISPRAVKA: Gradimo sumu iteracijom po ID-ovima sabirnica,
        // a ne po granama. Na ovaj nacin svaki susjedni cvor se pojavljuje
        // tacno jednom, bez obzira na broj paralelnih grana.
        td::MutableString sumRe, sumIm;
        sumRe.appendFormat("G_%d_%d*e_%d - B_%d_%d*f_%d", bId, bId, bId, bId, bId, bId);
        sumIm.appendFormat("G_%d_%d*f_%d + B_%d_%d*e_%d", bId, bId, bId, bId, bId, bId);

        for (int j = 1; j <= maxBusId; ++j)
        {
            if (j == bId) continue;
            // Gm(bId, j) == 0 i Bm(bId, j) == 0 znaci da nema veze — preskoci
            if (Gm(bId, j) == 0.0 && Bm(bId, j) == 0.0) continue;

            sumRe.appendFormat(" + G_%d_%d*e_%d - B_%d_%d*f_%d", bId, j, j, bId, j, j);
            sumIm.appendFormat(" + G_%d_%d*f_%d + B_%d_%d*e_%d", bId, j, j, bId, j, j);
        }

        if (imaGen)
        {
            mStr.appendFormat("\t// node %d - PV/SLACK (With Generator)\n", bId);
            mStr.appendFormat("\t%s = Id_g%d*sin(\xce\xb4_g%d) + Iq_g%d*cos(\xce\xb4_g%d) - P_%d_d*e_%d/(e_%d^2+f_%d^2) - Q_%d_d*f_%d/(e_%d^2+f_%d^2)\n",
                sumRe.c_str(), bId, bId, bId, bId, bId, bId, bId, bId, bId, bId, bId, bId);
            mStr.appendFormat("\t%s = -Id_g%d*cos(\xce\xb4_g%d) + Iq_g%d*sin(\xce\xb4_g%d) - P_%d_d*f_%d/(e_%d^2+f_%d^2) + Q_%d_d*e_%d/(e_%d^2+f_%d^2)\n",
                sumIm.c_str(), bId, bId, bId, bId, bId, bId, bId, bId, bId, bId, bId, bId);
        }
        else
        {
            bool isZI = (data.buses[i].Pd == 0.0 && data.buses[i].Qd == 0.0);

            if (isZI)
            {
                mStr.appendFormat("\t// node %d - ZI\n", bId);
                mStr.appendFormat("\t%s = -P_%d_d\n", sumRe.c_str(), bId);
                mStr.appendFormat("\t%s = -Q_%d_d\n", sumIm.c_str(), bId);
            }
            else
            {
                mStr.appendFormat("\t// node %d - PQ\n", bId);
                mStr.appendFormat("\t%s = -P_%d_d*e_%d/(e_%d^2+f_%d^2) - Q_%d_d*f_%d/(e_%d^2+f_%d^2)\n",
                    sumRe.c_str(), bId, bId, bId, bId, bId, bId, bId, bId);
                mStr.appendFormat("\t%s = -P_%d_d*f_%d/(e_%d^2+f_%d^2) + Q_%d_d*e_%d/(e_%d^2+f_%d^2)\n",
                    sumIm.c_str(), bId, bId, bId, bId, bId, bId, bId, bId);
            }
        }

        memDigitalOut.put(mStr.c_str(), mStr.length());
        
        mStr.reset();
    }
}

bool getAllBusInfo(const td::String& fileName,
    cnt::PushBackVector<int>& genBuses,
    cnt::PushBackVector<BusInfo>& allBuses,
    gui::LineEdit& status)
{
    MatpowerData data;
    if (!parseMatpower(fileName, data, status)) return false;

    genBuses.reserve(data.gens.size());
    for (const auto& g : data.gens)
        genBuses.push_back(g.bus);

    allBuses.reserve(data.buses.size());
    for (const auto& b : data.buses)
    {
        BusInfo bi;
        bi.busId = b.id;
        bi.origPd = b.Pd / data.baseMVA;
        bi.origQd = b.Qd / data.baseMVA;
        bi.isGenerator = false;
        for (size_t i = 0; i < data.gens.size(); ++i)
        {
            if (data.gens[i].bus == b.id)
            {
                bi.isGenerator = true;
                break;
            }
        }
        allBuses.push_back(bi);
    }
    return true;
}

void modelPostProc(arch::MemoryOut& memDigitalOut, td::MutableString& mStr,
    MatpowerData& data, const Options& options)
{
    if (options.loadProfiles.size() == 0) return;

    const char* s_begin = "PostProc:\n";
    const char* s_end = "end\n";
    memDigitalOut.put(s_begin, strlen(s_begin));
    
    for (size_t p = 0; p < options.loadProfiles.size(); ++p)
    {
        const BusLoadProfile& profile = options.loadProfiles[p];
        std::string busStr = std::to_string(profile.busId);

        for (size_t i = 0; i < profile.points.size(); ++i)
        {
            const TimePoint& pt = profile.points[i];
            std::string t = std::to_string(pt.t);
            std::string Pd = std::to_string(pt.Pd);
            std::string Qd = std::to_string(pt.Qd);

            std::string s1 = "\tif t >= " + t + ":\n";
            std::string s2 = "\t\tP_" + busStr + "_d = " + Pd + "\n";
            std::string s3 = "\t\tQ_" + busStr + "_d = " + Qd + "\n";
            std::string s4 = "\tend\n";

            memDigitalOut.put(s1.c_str(), s1.length());
            memDigitalOut.put(s2.c_str(), s2.length());
            memDigitalOut.put(s3.c_str(), s3.length());
            memDigitalOut.put(s4.c_str(), s4.length());
           
        }
    }

    memDigitalOut.put(s_end, strlen(s_end));
    
}

void visualModel(arch::MemoryOut& memVisualOut, td::MutableString& mStr,
    MatpowerData& data, const Options& options)
{
    const char* h1 = "Header:\n";
    const char* h2 = "\tnewTab = false\n";
    const char* h3 = "\tdrawPlots = true\n";
    const char* h4 = "end\n";
    const char* m1 = "Model [name=\"Visual of Dynamics\"]:\n";
    const char* p1 = "Plots:\n";

    memVisualOut.put(h1, strlen(h1));
    memVisualOut.put(h2, strlen(h2));
    memVisualOut.put(h3, strlen(h3));
    memVisualOut.put(h4, strlen(h4));
    memVisualOut.put(m1, strlen(m1));
    memVisualOut.put(p1, strlen(p1));

  
    struct ColorPair { const char* light; const char* dark; };
    ColorPair colors[] = {
        {"black",    "white"},
        {"darkRed",  "magenta"},
        {"darkBlue", "cyan"},
        {"darkGreen","green"},
        {"orange",   "yellow"},
        {"purple",   "pink"},
        {"brown",    "lightGray"},
        {"gray",     "darkGray"}
    };

    auto shouldShow = [&](int busId) -> bool {
        if (options.selectedBuses.size() == 0) return true;
        for (size_t i = 0; i < options.selectedBuses.size(); ++i)
            if (options.selectedBuses[i] == busId) return true;
        return false;
        };

    size_t numColors = sizeof(colors) / sizeof(colors[0]);

    td::MutableString vStr;
    vStr.reserve(256);

    const char* plotEnd = "\tend\n\n";

    // 1. GRAFIKON: BRZINE GENERATORA
    if (options.showSpeeds)
    {
        const char* hdr = "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Speed [p.u.]\" name=\"Gen. speeds\" anchor=TopRight legend=true nCols=1 anchorX=160 anchorY=50]:\n\t\t@x << t\n";
        memVisualOut.put(hdr, strlen(hdr));
       

        for (size_t i = 0; i < data.gens.size(); ++i)
        {
            int k = data.gens[i].bus;
            if (!shouldShow(k)) continue;
            ColorPair cp = colors[i % numColors];
            vStr.reset();
            vStr.appendFormat("\t\t@y << \xcf\x89_g%d [colorL=%s colorD=%s width=2 pattern=\"solid\" name=\"\xcf\x89_g%d\"]\n",
                k, cp.light, cp.dark, k);
            memVisualOut.put(vStr.c_str(), vStr.length());
           
        }

        const char* footer = "\t\t@y << \xcf\x89_ref [colorL=red colorD=red width=1 pattern=\"dot\" name=\"Ref. \xcf\x89\"]\n\tend\n\n";
        memVisualOut.put(footer, strlen(footer));
       
    }

    // 2. GRAFIKON: MEHANICKE SNAGE
    if (options.showPowers)
    {
        const char* hdr = "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Power [p.u.]\" name=\"Gen. Powers\" anchor=TopRight legend=true nCols=1 anchorX=160 anchorY=50]:\n\t\t@x << t\n";
        memVisualOut.put(hdr, strlen(hdr));
        

        for (size_t i = 0; i < data.gens.size(); ++i)
        {
            int k = data.gens[i].bus;
            if (!shouldShow(k)) continue;
            ColorPair cp = colors[i % numColors];
            vStr.reset();
            vStr.appendFormat("\t\t@y << P_gml_%d [colorL=%s colorD=%s width=2 name=\"Pgm_%d\"]\n",
                k, cp.light, cp.dark, k);
            memVisualOut.put(vStr.c_str(), vStr.length());
           
        }

        memVisualOut.put(plotEnd, strlen(plotEnd));
       
    }

    // 3. GRAFIKON: ROTORSKI UGLOVI
    if (options.showAngles)
    {
        const char* hdr = "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Angle [rad]\" name=\"Rotor Angles\" anchor=TopRight legend=true nCols=1 anchorX=160 anchorY=50]:\n\t\t@x << t\n";
        memVisualOut.put(hdr, strlen(hdr));
       

        for (size_t i = 0; i < data.gens.size(); ++i)
        {
            int k = data.gens[i].bus;
            if (!shouldShow(k)) continue;
            ColorPair cp = colors[i % numColors];
            vStr.reset();
            vStr.appendFormat("\t\t@y << \xce\xb4_g%d [colorL=%s colorD=%s width=2 pattern=\"solid\" name=\"\xce\xb4_g%d\"]\n",
                k, cp.light, cp.dark, k);
            memVisualOut.put(vStr.c_str(), vStr.length());
            
        }

        memVisualOut.put(plotEnd, strlen(plotEnd));
       
    }

    // 4. GRAFIKON: NAPONI
    if (options.showVoltages)
    {
        const char* hdr = "\tlinePlot [xLabel=\"Time [s]\" yLabel=\"Voltage [p.u.]\" "
            "name=\"Bus Voltages\" anchor=TopRight legend=true "
            "nCols=1 anchorX=160 anchorY=50]:\n\t\t@x << t\n";
        memVisualOut.put(hdr, strlen(hdr));
        

        auto isGenBus = [&](int busId) -> bool {
            for (size_t i = 0; i < data.gens.size(); ++i)
                if (data.gens[i].bus == busId) return true;
            return false;
            };

        if (options.selectedBuses.size() == 0)
        {
            size_t colorIdx = 0;
            for (size_t i = 0; i < data.gens.size(); ++i)
            {
                int k = data.gens[i].bus;
                ColorPair cp = colors[colorIdx++ % numColors];
                vStr.reset();
                vStr.appendFormat("\t\t@y << Vt_g%d [colorL=%s colorD=%s width=2 name=\"Vt_g%d\"]\n",
                    k, cp.light, cp.dark, k);
                memVisualOut.put(vStr.c_str(), vStr.length());
                
            }
            for (size_t i = 0; i < data.buses.size(); ++i)
            {
                int k = data.buses[i].id;
                if (isGenBus(k)) continue;
                ColorPair cp = colors[colorIdx++ % numColors];
                vStr.reset();
                vStr.appendFormat("\t\t@y << Vt_%d [colorL=%s colorD=%s width=2 name=\"Vt_%d\"]\n",
                    k, cp.light, cp.dark, k);
                memVisualOut.put(vStr.c_str(), vStr.length());
               
            }
        }
        else
        {
            for (size_t i = 0; i < options.selectedBuses.size(); ++i)
            {
                int k = options.selectedBuses[i];
                ColorPair cp = colors[i % numColors];
                vStr.reset();
                if (isGenBus(k))
                    vStr.appendFormat("\t\t@y << Vt_g%d [colorL=%s colorD=%s width=2 name=\"Vt_g%d\"]\n",
                        k, cp.light, cp.dark, k);
                else
                    vStr.appendFormat("\t\t@y << Vt_%d [colorL=%s colorD=%s width=2 name=\"Vt_%d\"]\n",
                        k, cp.light, cp.dark, k);
                memVisualOut.put(vStr.c_str(), vStr.length());
               
            }
        }

        memVisualOut.put(plotEnd, strlen(plotEnd));
       
    }

    const char* closeModel = "end\n";
    memVisualOut.put(closeModel, strlen(closeModel));
    

    vStr.reset();
}

bool createModel(const td::String& inputFileName, const td::String& outFileName,
    sc::IPlugin* pIPlugin, const Options& options, gui::LineEdit& status,
    const std::function<void(double)>& onProgress)
{
    // 16 vecih faza ukupno - fraction = step/totalSteps. Bezbjedno se
    // poziva samo ako je onProgress zadan (prazan std::function se ovdje
    // ne poziva).
    const int totalSteps = 16;
    int step = 0;
    auto reportProgress = [&]() {
        if (onProgress) onProgress((double)(++step) / totalSteps);
        };

    mu::ScopedCLocale scopedLocale;
    MatpowerData data;
    if (!parseMatpower(inputFileName, data, status))
        return false;
    reportProgress(); // 1

    auto pDigitModel = pIPlugin->getArchive(sc::IPlugin::ArchType::DigitalModel);
    auto& memDigitalOut = *pDigitModel;
    td::MutableString mStr;
    mStr.reserve(16384);

    modelHeader(memDigitalOut, mStr);           reportProgress(); // 2
    modelVarsInit(memDigitalOut, mStr, data);    reportProgress(); // 3
    dynGenVarsInit(memDigitalOut, mStr, data.gens); reportProgress(); // 4
    modelParams(memDigitalOut, mStr, data);      reportProgress(); // 5
    dynGenParameters(memDigitalOut, mStr, data.gens, options.genParams); reportProgress(); // 6
    submodelVarsInit(memDigitalOut, mStr, data); reportProgress(); // 7
    submodelParamsInit(memDigitalOut, mStr, data.gens); reportProgress(); // 8
    submodelNLE(memDigitalOut, mStr, data);      reportProgress(); // 9
    submodelPostProc(memDigitalOut, mStr, data); reportProgress(); // 10
    modelODE(memDigitalOut, mStr, data.gens);    reportProgress(); // 11
    modelNLE(memDigitalOut, mStr, data);         reportProgress(); // 12
    modelPostProc(memDigitalOut, mStr, data, options); reportProgress(); // 13

    std::ofstream fDigital;
    if (!fo::createTextFile(fDigital, outFileName))
    {
        status = "ERROR! Cannot create output file!";
        return false;
    }
    memDigitalOut.writeToFile(fDigital);
    fDigital.close();
    reportProgress(); // 14

    auto pVisualModel = pIPlugin->getArchive(sc::IPlugin::ArchType::VisualModel);
    auto& memVisualOut = *pVisualModel;

    visualModel(memVisualOut, mStr, data, options);
    reportProgress(); // 15

    td::String strVisualFileName = fo::replaceFileExtension<false>(outFileName, ".vmodl");
    std::ofstream fVisual;
    if (!fo::createTextFile(fVisual, strVisualFileName))
    {
        status = "ERROR! Cannot create visual output file!";
        return false;
    }
    memVisualOut.writeToFile(fVisual);
    fVisual.close();
    reportProgress(); // 16

    status = "OK!";
    return true;
}