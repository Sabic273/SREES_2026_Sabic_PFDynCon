#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/CheckBox.h>
#include <gui/Button.h>
#include <gui/NumericEdit.h>
#include <gui/LineEdit.h>
#include <gui/HorizontalLayout.h>
#include <gui/VerticalLayout.h>
#include <gui/GridLayout.h>
#include <gui/ProgressIndicator.h>
#include <gui/FileDialog.h>
#include <cnt/PushBackVector.h>
#include <vector>
#include <sstream>
#include <string>
#include "PowerFlowPlugin.h"

class ViewCreateModel : public gui::View
{
    static const int    MAX_EVENTS = 5;
    static const int    CB_ROWS_PER_COL = 5;
    static const int    CB_MAX_COLS = 5;
    static const size_t LOAD_THRESHOLD = 40;

    static int computeNumEventCols(size_t nBuses)
    {
        if (nBuses > 32) return 5;
        if (nBuses > 24) return 4;
        if (nBuses > 16) return 3;
        if (nBuses > 8) return 2;
        return 1;
    }

    // Svim granama je dodata GEN sekcija (label + hlRow + status = 3 reda),
    // dostupna bez obzira na velicinu sistema, plus 2 appendSpace() razmaka
    // (nakon Plot Types i nakon GEN sekcije) koji se racunaju kao redovi u
    // VerticalLayout-u. Poremecaji-iz-fajla sekcija (label + hlRow + status
    // + appendSpace = 4 reda) je TAKODJER sada univerzalna (ranije samo za
    // large). Small grane imaju jos 1 appendSpace() prije "Poremecaji"
    // naslova. Cijela tabela poremecaja (header + svi redovi) je JEDAN
    // GridLayout -> 1 red u _vl, isto za obje small varijante (<=40 i
    // 41-50), pa se konacne vrijednosti poklapaju. +2 za _lblProgress i
    // _progressBar (ideja #6). Plot Types checkboxovi su sada u JEDNOM
    // HorizontalLayout-u (label + hlPlotTypes = 2 reda, umjesto 5).
    // Precizno prebrojano (linija po linija): Large: 15. Small (obje
    // varijante): 21.
    static size_t computeVlSize(size_t nAll)
    {
        if (nAll > 50) return 15;
        return 21;
    }

    sc::IPlugin* _pIPlugin;
    sc::IPlugin::CallBack _onComplete;
    td::String            _inputFileName;
    td::String            _outFileName;
    td::UINT4             _wndID;

    bool _isLargeSystem;  // > 50 buseva
    bool _useCheckBoxes;  // <= 40 buseva
    bool _useBusRows;     // <= 40 buseva
    bool _useFileEvents;
    int  _numEventCols;

    cnt::PushBackVector<BusInfo>        _displayBuses;
    cnt::PushBackVector<gui::CheckBox*> _busCheckBoxes;
    std::vector<BusLoadProfile>         _fileLoadedProfiles;
    std::vector<int>                    _fileSelectedBuses; // cvorovi za prikaz, parsirani iz "PLOT" reda u fajlu (samo large)
    std::vector<GenDynParams>           _fileGenParams;     // dinamicki parametri generatora, parsirani iz GEN fajla (small i large)

    // Checkbox kolone
    gui::HorizontalLayout* _hlCbCols = nullptr;
    cnt::PushBackVector<gui::VerticalLayout*> _cbColLayouts;

    // Plot types
    gui::Label    _lblPlotTypes;
    gui::CheckBox _cbSpeeds;
    gui::CheckBox _cbPowers;
    gui::CheckBox _cbAngles;
    gui::CheckBox _cbVoltages;
    gui::HorizontalLayout _hlPlotTypes;

    // Bus selekcija (small: checkbox/lista; large: range filter za prikaz)
    gui::Label    _lblBusSel;
    gui::CheckBox _cbAllBuses;
    gui::Label    _lblBusRange;
    gui::LineEdit _editBusRange;

    // Poremecaji manual (samo small)
    gui::Label _lblEvents;
    cnt::PushBackVector<gui::Label*> _eventHdrLabels; // "Cvor/t_start/t_end/newP/newQ" naslovi, po 5 za svaku kolonu

    struct BusEventRow {
        int                    busId = 0;
        gui::CheckBox* pCb = nullptr;
        gui::NumericEdit* pTStart = nullptr;
        gui::NumericEdit* pTEnd = nullptr;
        gui::NumericEdit* pNewPd = nullptr;
        gui::NumericEdit* pNewQd = nullptr;
    };
    cnt::PushBackVector<BusEventRow> _busEventRows;

    struct EventRow {
        gui::NumericEdit* pBusId = nullptr;
        gui::NumericEdit* pTStart = nullptr;
        gui::NumericEdit* pTEnd = nullptr;
        gui::NumericEdit* pNewPd = nullptr;
        gui::NumericEdit* pNewQd = nullptr;
    } _eventRows[MAX_EVENTS];

    // GridLayout garantuje da SVI redovi (header + podaci) dijele iste
    // sirine kolona - kljucno za poravnanje kad ima vise kolona cvorova
    // (za razliku od stacka nezavisnih HorizontalLayout redova, gdje
    // sirina svakog reda zavisi samo od NJEGOVOG sadrzaja).
    gui::GridLayout* _eventsGrid = nullptr;

    // Poremecaji iz fajla (samo large)
    gui::Label    _lblFileEvents;
    gui::LineEdit _editFilePath;
    gui::Button   _btnBrowseFile;
    gui::Button   _btnClearFile;
    gui::Label    _lblFileStatus;
    gui::HorizontalLayout _hlFileRow;

    // Dinamicki parametri generatora iz fajla (small i large)
    gui::Label    _lblGenFile;
    gui::LineEdit _editGenFilePath;
    gui::Button   _btnBrowseGenFile;
    gui::Button   _btnClearGenFile;
    gui::Label    _lblGenFileStatus;
    gui::HorizontalLayout _hlGenFileRow;

    // Dugmad
    gui::Button           _btnConvert;
    gui::Button           _btnCancel;
    gui::HorizontalLayout _hlButtons;

    // Progress indicator za generisanje modela (ideja #6) - vrijednost 0..1,
    // azurira se preko onProgress callback-a koji createModel() poziva
    // nakon svake vece faze.
    gui::Label             _lblProgress;
    gui::ProgressIndicator _progressBar;

    gui::VerticalLayout _vl;

    void setManualEventsEnabled(bool en)
    {
        for (size_t i = 0; i < _busEventRows.size(); ++i)
        {
            _busEventRows[i].pCb->enable(en);
            if (!en)
            {
                _busEventRows[i].pTStart->enable(false);
                _busEventRows[i].pTEnd->enable(false);
                _busEventRows[i].pNewPd->enable(false);
                _busEventRows[i].pNewQd->enable(false);
            }
        }
        for (int i = 0; i < MAX_EVENTS; ++i)
        {
            if (_eventRows[i].pBusId)  _eventRows[i].pBusId->enable(en);
            if (_eventRows[i].pTStart) _eventRows[i].pTStart->enable(en);
            if (_eventRows[i].pTEnd)   _eventRows[i].pTEnd->enable(en);
            if (_eventRows[i].pNewPd)  _eventRows[i].pNewPd->enable(en);
            if (_eventRows[i].pNewQd)  _eventRows[i].pNewQd->enable(en);
        }
    }

public:
    gui::LineEdit _editStatus;

    static void parseRangeString(const td::String& input, cnt::PushBackVector<int>& out)
    {
        std::string str(input.c_str());
        std::stringstream ss(str);
        std::string token;
        out.reserve(64);
        while (std::getline(ss, token, ','))
        {
            size_t s = token.find_first_not_of(" \t");
            size_t e = token.find_last_not_of(" \t");
            if (s == std::string::npos) continue;
            token = token.substr(s, e - s + 1);
            size_t dash = token.find('-');
            if (dash != std::string::npos && dash > 0)
            {
                try {
                    int from = std::stoi(token.substr(0, dash));
                    int to = std::stoi(token.substr(dash + 1));
                    for (int i = from; i <= to; ++i) out.push_back(i);
                }
                catch (...) {}
            }
            else { try { out.push_back(std::stoi(token)); } catch (...) {} }
        }
    }

    ViewCreateModel(sc::IPlugin* pIPlugin,
        const td::String& inputFileName,
        const td::String& outFileName,
        const sc::IPlugin::CallBack& onComplete,
        const cnt::PushBackVector<BusInfo>& allBuses,
        td::UINT4                            wndID = 0)
        : _pIPlugin(pIPlugin)
        , _onComplete(onComplete)
        , _inputFileName(inputFileName)
        , _outFileName(outFileName)
        , _wndID(wndID)
        , _isLargeSystem(allBuses.size() > 50)
        , _useCheckBoxes(!_isLargeSystem && allBuses.size() <= LOAD_THRESHOLD)
        , _useBusRows(!_isLargeSystem && allBuses.size() <= LOAD_THRESHOLD)
        , _useFileEvents(false)
        , _numEventCols(computeNumEventCols(allBuses.size()))
        , _lblPlotTypes("Grafici:")
        , _cbSpeeds("Ugaone brzine")
        , _cbPowers("Mehanicke snage")
        , _cbAngles("Rotorski uglovi")
        , _cbVoltages("Naponi")
        , _hlPlotTypes(4)
        , _lblBusSel("Prikaz cvorova:")
        , _cbAllBuses("Svi cvorovi")
        , _lblBusRange("Cvorovi za prikaz (\"all\" ili \"1,3,5-9\"):")
        , _lblEvents("Poremecaji tereta (manual):")
        , _lblFileEvents("Poremecaji iz fajla (BUS id / t Pd Qd / END) - opciono, ima prioritet nad rucnim unosom ispod:")
        , _btnBrowseFile("Ucitaj fajl")
        , _btnClearFile("Resetuj")
        , _lblFileStatus("")
        , _hlFileRow(3)
        , _lblGenFile("Parametri generatora (GEN id / kljuc=vrijednost / END):")
        , _btnBrowseGenFile("Ucitaj fajl")
        , _btnClearGenFile("Resetuj")
        , _lblGenFileStatus("")
        , _hlGenFileRow(3)
        , _btnConvert("Convert")
        , _btnCancel("Cancel")
        , _hlButtons(3)
        , _lblProgress("")
        , _progressBar(gui::DataCtrl::Orientation::Horizontal, true)
        , _vl(computeVlSize(allBuses.size()))
    {
        _displayBuses.reserve(allBuses.size());
        for (size_t i = 0; i < allBuses.size(); ++i)
            _displayBuses.push_back(allBuses[i]);

        _cbSpeeds.setChecked(true);
        _cbPowers.setChecked(true);
        _cbAngles.setChecked(true);
        _cbVoltages.setChecked(true);
        _editStatus.setAsReadOnly();
        _editFilePath.setAsReadOnly();
        _editGenFilePath.setAsReadOnly();

        // ── Plot types — uvijek ───────────────────────────────────
        _lblPlotTypes.setFont(gui::Font::ID::SystemLargerBold);
        _hlPlotTypes << _cbSpeeds << _cbPowers << _cbAngles << _cbVoltages;
        _vl << _lblPlotTypes << _hlPlotTypes;
        _vl.appendSpace(10);

        // ── Dinamicki parametri generatora — uvijek (small i large) ─
        _lblGenFile.setFont(gui::Font::ID::SystemLargerBold);
        // setResizable() je NEOPHODAN da bi Label ispravno promijenio
        // visinu/sirinu kada mu se tekst kasnije (u onClick handleru)
        // promijeni na duzi (npr. "OK! Custom parametri..."). Bez ovoga
        // ostaje "zamrznut" na velicini iz trenutka konstrukcije (kad je
        // tekst bio ""), pa dugi tekst kasnije preklopi ostale redove.
        _lblGenFileStatus.setResizable(80, td::TextEllipsize::End);
        _hlGenFileRow << _editGenFilePath << _btnBrowseGenFile << _btnClearGenFile;
        _vl << _lblGenFile << _hlGenFileRow << _lblGenFileStatus;
        _vl.appendSpace(10);

        // ── Poremecaji iz fajla — uvijek (small i large) ────────────
        // Za small sisteme, fajl je OPCIONALAN i ima prioritet nad
        // rucnim unosom ispod ako je uspjesno ucitan (vidi getOptions()).
        _lblFileEvents.setFont(gui::Font::ID::SystemLargerBold);
        _lblFileStatus.setResizable(80, td::TextEllipsize::End);
        _hlFileRow << _editFilePath << _btnBrowseFile << _btnClearFile;
        _vl << _lblFileEvents << _hlFileRow << _lblFileStatus;
        _vl.appendSpace(10);

        if (_isLargeSystem)
        {
            // ── Large: samo file sekcija (vec dodana gore) ───────
            // Cvorovi za prikaz na graficima se ne unose ovdje preko GUI-a;
            // navode se unutar samog fajla poremecaja (vidi "PLOT" red u
            // parseLoadEventsFile / opisu formata fajla).
        }
        else
        {
            // ── Small: bus selekcija + manual eventi (alternativa fajlu) ─
            _lblBusSel.setFont(gui::Font::ID::SystemLargerBold);
            _vl << _lblBusSel;

            if (_useCheckBoxes)
            {
                _cbAllBuses.setChecked(true);
                _vl << _cbAllBuses;

                int nBuses = (int)allBuses.size();
                int numCbCols = (nBuses + CB_ROWS_PER_COL - 1) / CB_ROWS_PER_COL;
                if (numCbCols > CB_MAX_COLS) numCbCols = CB_MAX_COLS;
                if (numCbCols < 1) numCbCols = 1;
                int rowsPerCbCol = (nBuses + numCbCols - 1) / numCbCols;

                _hlCbCols = new gui::HorizontalLayout(numCbCols);
                _cbColLayouts.reserve(numCbCols);
                for (int c = 0; c < numCbCols; ++c)
                {
                    int start = c * rowsPerCbCol;
                    int end = (start + rowsPerCbCol < nBuses) ? (start + rowsPerCbCol) : nBuses;
                    _cbColLayouts.push_back(new gui::VerticalLayout(end - start > 0 ? end - start : 1));
                }

                _busCheckBoxes.reserve(allBuses.size());
                for (size_t i = 0; i < allBuses.size(); ++i)
                {
                    char buf[80];
                    snprintf(buf, sizeof(buf), "%s %d (P=%.2f Q=%.2f)",
                        allBuses[i].isGenerator ? "Gen " : "Cvor",
                        allBuses[i].busId,
                        allBuses[i].origPd, allBuses[i].origQd);
                    auto pCb = new gui::CheckBox(buf);
                    pCb->setChecked(true);
                    pCb->enable(false);
                    _busCheckBoxes.push_back(pCb);
                    int col = (int)i / rowsPerCbCol;
                    if (col >= numCbCols) col = numCbCols - 1;
                    *_cbColLayouts[col] << *pCb;
                }
                for (int c = 0; c < numCbCols; ++c)
                    *_hlCbCols << *_cbColLayouts[c];
                _vl << *_hlCbCols;

                _cbAllBuses.onClick([this] {
                    bool all = _cbAllBuses.isChecked();
                    for (auto pCb : _busCheckBoxes)
                    {
                        pCb->enable(!all);
                        if (all) pCb->setChecked(true);
                    }
                    });
            }
            else
            {
                _editBusRange = "all";
                _vl << _lblBusRange << _editBusRange;
            }

            _vl.appendSpace(8);
            _lblEvents.setFont(gui::Font::ID::SystemLargerBold);
            _vl << _lblEvents;

            if (_useBusRows)
            {
                int nBuses = (int)allBuses.size();
                int rowsPerCol = (nBuses + _numEventCols - 1) / _numEventCols;

                static const char* s_hdrTexts[5] = { "Cvor", "t_start", "t_end", "newP", "newQ" };

                td::BYTE nGridRows = (td::BYTE)(rowsPerCol + 1); // +1 header red
                td::BYTE nGridCols = (td::BYTE)(_numEventCols * 5);
                _eventsGrid = new gui::GridLayout(nGridRows, nGridCols);
                _eventsGrid->setSpaceBetweenCells(4, 4);

                _eventHdrLabels.reserve(_numEventCols * 5);
                for (int c = 0; c < _numEventCols; ++c)
                {
                    for (int k = 0; k < 5; ++k)
                    {
                        auto pLbl = new gui::Label(s_hdrTexts[k]);
                        pLbl->setFont(gui::Font::ID::SystemBold);
                        _eventHdrLabels.push_back(pLbl);
                        _eventsGrid->insert(0, (td::BYTE)(c * 5 + k), *pLbl, td::HAlignment::Center);
                    }
                }
                _busEventRows.reserve(allBuses.size());
                for (size_t i = 0; i < allBuses.size(); ++i)
                {
                    BusEventRow row;
                    row.busId = allBuses[i].busId;
                    char buf[80];
                    snprintf(buf, sizeof(buf), "C%d%s P=%.2f Q=%.2f",
                        allBuses[i].busId,
                        allBuses[i].isGenerator ? "[G]" : "   ",
                        allBuses[i].origPd, allBuses[i].origQd);
                    row.pCb = new gui::CheckBox(buf);
                    row.pTStart = new gui::NumericEdit(td::real4);
                    row.pTEnd = new gui::NumericEdit(td::real4);
                    row.pNewPd = new gui::NumericEdit(td::real4);
                    row.pNewQd = new gui::NumericEdit(td::real4);
                    row.pCb->setChecked(false);
                    row.pTStart->setValue(td::Variant(0.5f));
                    row.pTEnd->setValue(td::Variant(6.0f));
                    row.pNewPd->setValue(td::Variant(0.0f));
                    row.pNewQd->setValue(td::Variant(0.0f));
                    row.pTStart->enable(false);
                    row.pTEnd->enable(false);
                    row.pNewPd->enable(false);
                    row.pNewQd->enable(false);
                    _busEventRows.push_back(row);
                }

                for (size_t i = 0; i < _busEventRows.size(); ++i)
                {
                    gui::CheckBox* pCb = _busEventRows[i].pCb;
                    gui::NumericEdit* pTS = _busEventRows[i].pTStart;
                    gui::NumericEdit* pTE = _busEventRows[i].pTEnd;
                    gui::NumericEdit* pNP = _busEventRows[i].pNewPd;
                    gui::NumericEdit* pNQ = _busEventRows[i].pNewQd;
                    pCb->onClick([pCb, pTS, pTE, pNP, pNQ] {
                        bool en = pCb->isChecked();
                        pTS->enable(en); pTE->enable(en);
                        pNP->enable(en); pNQ->enable(en);
                        });
                }

                // Ubacivanje u grid - svaka kolona-grupa cvorova zauzima 5
                // grid-kolona (baseCol..baseCol+4), redovi pocinju od 1
                // (red 0 je zajednicki header za tu grupu).
                for (size_t i = 0; i < _busEventRows.size(); ++i)
                {
                    int col = (int)i / rowsPerCol;
                    if (col >= _numEventCols) col = _numEventCols - 1;
                    int rowInCol = (int)i - col * rowsPerCol;
                    td::BYTE gridRow = (td::BYTE)(1 + rowInCol);
                    td::BYTE baseCol = (td::BYTE)(col * 5);
                    _eventsGrid->insert(gridRow, (td::BYTE)(baseCol + 0), *_busEventRows[i].pCb);
                    _eventsGrid->insert(gridRow, (td::BYTE)(baseCol + 1), *_busEventRows[i].pTStart);
                    _eventsGrid->insert(gridRow, (td::BYTE)(baseCol + 2), *_busEventRows[i].pTEnd);
                    _eventsGrid->insert(gridRow, (td::BYTE)(baseCol + 3), *_busEventRows[i].pNewPd);
                    _eventsGrid->insert(gridRow, (td::BYTE)(baseCol + 4), *_busEventRows[i].pNewQd);
                }

                _vl << *_eventsGrid;
            }
            else
            {
                static const char* s_hdrTexts2[5] = { "Cvor", "t_start", "t_end", "newP", "newQ" };
                _eventsGrid = new gui::GridLayout((td::BYTE)(MAX_EVENTS + 1), 5);
                _eventsGrid->setSpaceBetweenCells(4, 4);

                for (int k = 0; k < 5; ++k)
                {
                    auto pLbl = new gui::Label(s_hdrTexts2[k]);
                    pLbl->setFont(gui::Font::ID::SystemBold);
                    _eventHdrLabels.push_back(pLbl);
                    _eventsGrid->insert(0, (td::BYTE)k, *pLbl, td::HAlignment::Center);
                }

                for (int i = 0; i < MAX_EVENTS; ++i)
                {
                    _eventRows[i].pBusId = new gui::NumericEdit(td::int4);
                    _eventRows[i].pTStart = new gui::NumericEdit(td::real4);
                    _eventRows[i].pTEnd = new gui::NumericEdit(td::real4);
                    _eventRows[i].pNewPd = new gui::NumericEdit(td::real4);
                    _eventRows[i].pNewQd = new gui::NumericEdit(td::real4);
                    _eventRows[i].pBusId->setValue(td::Variant((td::INT4)0));
                    _eventRows[i].pTStart->setValue(td::Variant(0.5f));
                    _eventRows[i].pTEnd->setValue(td::Variant(6.0f));
                    _eventRows[i].pNewPd->setValue(td::Variant(0.0f));
                    _eventRows[i].pNewQd->setValue(td::Variant(0.0f));

                    td::BYTE gridRow = (td::BYTE)(1 + i);
                    _eventsGrid->insert(gridRow, 0, *_eventRows[i].pBusId);
                    _eventsGrid->insert(gridRow, 1, *_eventRows[i].pTStart);
                    _eventsGrid->insert(gridRow, 2, *_eventRows[i].pTEnd);
                    _eventsGrid->insert(gridRow, 3, *_eventRows[i].pNewPd);
                    _eventsGrid->insert(gridRow, 4, *_eventRows[i].pNewQd);
                }

                _vl << *_eventsGrid;
            }
        }

        // ── Status i dugmad — uvijek ─────────────────────────────
        _btnConvert.setFont(gui::Font::ID::SystemLargerBold);
        _progressBar.setValue(0.0);
        _lblProgress.setResizable(40, td::TextEllipsize::End);
        _hlButtons.appendSpacer() << _btnCancel << _btnConvert;
        _vl << _lblProgress << _progressBar << _editStatus << _hlButtons;
        setLayout(&_vl);

        // ── File handler — uvijek (small i large) ────────────────
        _btnBrowseFile.onClick([this] {
            gui::OpenFileDialog::show(this, "Otvori fajl poremecaja", "*.txt",
                _wndID + 100,
                [this](gui::FileDialog* pDlg)
                {
                    if (pDlg->getStatus() != gui::FileDialog::Status::OK) return;
                    td::String fileName = pDlg->getFileName();
                    if (fileName.isEmpty()) return;
                    _fileLoadedProfiles.clear();
                    _fileSelectedBuses.clear();
                    if (parseLoadEventsFile(fileName, _displayBuses,
                        _fileLoadedProfiles, _fileSelectedBuses, _editStatus))
                    {
                        _useFileEvents = true;
                        _editFilePath = fileName;

                        // Sastavi citljivu listu PLOT cvorova radi provjere
                        // (max 15 prikazanih, ostatak skraceno sa "...").
                        std::string busListStr;
                        size_t nShow = _fileSelectedBuses.size() < 15 ? _fileSelectedBuses.size() : 15;
                        for (size_t i = 0; i < nShow; ++i)
                        {
                            if (i > 0) busListStr += ",";
                            busListStr += std::to_string(_fileSelectedBuses[i]);
                        }
                        if (_fileSelectedBuses.size() > nShow) busListStr += ",...";

                        char buf[256];
                        if (_fileSelectedBuses.size() > 0)
                            snprintf(buf, sizeof(buf), "OK! %d profila. PLOT (%d): %s",
                                (int)_fileLoadedProfiles.size(), (int)_fileSelectedBuses.size(),
                                busListStr.c_str());
                        else
                            snprintf(buf, sizeof(buf), "OK! %d profila. PLOT nije naveden - prikazuju se SVI cvorovi.",
                                (int)_fileLoadedProfiles.size());
                        _lblFileStatus = buf;
                        _lblFileStatus.setTextColor(td::Accent::Success);

                        if (!_isLargeSystem)
                        {
                            // Rucni unos (checkboxovi/redovi) postaje irelevantan
                            // dok je fajl ucitan - onemoguci ga da se izbjegne
                            // zabuna o tome sta se stvarno koristi.
                            setManualEventsEnabled(false);
                        }
                    }
                    else
                    {
                        _useFileEvents = false;
                        _fileLoadedProfiles.clear();
                        _fileSelectedBuses.clear();
                        _editFilePath = "";
                        _lblFileStatus = "Greska pri ucitavanju!";
                        _lblFileStatus.setTextColor(td::Accent::Error);
                    }
                });
            });

        _btnClearFile.onClick([this] {
            _useFileEvents = false;
            _fileLoadedProfiles.clear();
            _fileSelectedBuses.clear();
            _editFilePath = "";
            _lblFileStatus = "";
            if (!_isLargeSystem)
            {
                setManualEventsEnabled(true);
            }
            });

        // ── GEN params fajl handler — uvijek (small i large) ────────
        _btnBrowseGenFile.onClick([this] {
            gui::OpenFileDialog::show(this, "Otvori fajl parametara generatora", "*.txt",
                _wndID + 200,
                [this](gui::FileDialog* pDlg)
                {
                    if (pDlg->getStatus() != gui::FileDialog::Status::OK) return;
                    td::String fileName = pDlg->getFileName();
                    if (fileName.isEmpty()) return;
                    _fileGenParams.clear();
                    if (parseGenParamsFile(fileName, _displayBuses, _fileGenParams, _editStatus))
                    {
                        _editGenFilePath = fileName;

                        // Sastavi citljivu listu ID-eva generatora radi provjere.
                        std::string genListStr;
                        size_t nShow = _fileGenParams.size() < 15 ? _fileGenParams.size() : 15;
                        for (size_t i = 0; i < nShow; ++i)
                        {
                            if (i > 0) genListStr += ",";
                            genListStr += std::to_string(_fileGenParams[i].busId);
                        }
                        if (_fileGenParams.size() > nShow) genListStr += ",...";

                        char buf[256];
                        snprintf(buf, sizeof(buf), "OK! Custom parametri za %d generatora: %s",
                            (int)_fileGenParams.size(), genListStr.c_str());
                        _lblGenFileStatus = buf;
                        _lblGenFileStatus.setTextColor(td::Accent::Success);
                    }
                    else
                    {
                        _fileGenParams.clear();
                        _editGenFilePath = "";
                        _lblGenFileStatus = "Greska pri ucitavanju!";
                        _lblGenFileStatus.setTextColor(td::Accent::Error);
                    }
                });
            });

        _btnClearGenFile.onClick([this] {
            _fileGenParams.clear();
            _editGenFilePath = "";
            _lblGenFileStatus = "";
            });

        // ── Convert/Cancel ───────────────────────────────────────
        _btnConvert.onClick([this] {
            Options opt = getOptions();
            _progressBar.setValue(0.0);
            _lblProgress = "Generisanje modela u toku...";
            auto onProgress = [this](double frac) {
                _progressBar.setValue(frac);
                };
            if (!createModel(_inputFileName, _outFileName, _pIPlugin, opt, _editStatus, onProgress))
            {
                _editStatus.setTextColor(td::Accent::Error);
                _lblProgress = "";
                _progressBar.setValue(0.0);
                return;
            }
            _editStatus.setTextColor(td::Accent::Success);
            _lblProgress = "Zavrseno.";
            sc::IPlugin* pPlugin = _pIPlugin;
            sc::IPlugin::CallBack onComplete = _onComplete;
            gui::Window* pCreateWnd = getParentWindow();
            gui::Window* pPluginWnd = pCreateWnd ? pCreateWnd->getParent() : nullptr;
            onComplete(pPlugin);
            onClosedPluginWindow();
            if (pCreateWnd) pCreateWnd->close();
            if (pPluginWnd) pPluginWnd->close();
            });

        _btnCancel.onClick([this] {
            gui::Window* pWnd = getParentWindow();
            if (pWnd) pWnd->close();
            });
    }

    ~ViewCreateModel()
    {
        for (auto pCb : _busCheckBoxes) delete pCb;
        delete _hlCbCols;
        for (size_t i = 0; i < _cbColLayouts.size(); ++i)    delete _cbColLayouts[i];

        for (size_t i = 0; i < _busEventRows.size(); ++i)
        {
            delete _busEventRows[i].pCb;
            delete _busEventRows[i].pTStart;
            delete _busEventRows[i].pTEnd;
            delete _busEventRows[i].pNewPd;
            delete _busEventRows[i].pNewQd;
        }
        for (size_t i = 0; i < _eventHdrLabels.size(); ++i)  delete _eventHdrLabels[i];
        delete _eventsGrid;

        for (int i = 0; i < MAX_EVENTS; ++i)
        {
            delete _eventRows[i].pBusId;
            delete _eventRows[i].pTStart;
            delete _eventRows[i].pTEnd;
            delete _eventRows[i].pNewPd;
            delete _eventRows[i].pNewQd;
        }
    }

    Options getOptions() const
    {
        Options opt;
        opt.modelName = "Power Flow Model";
        opt.maxIter = 10;
        opt.dTime = 0.001f;
        opt.endTime = 60.f;
        opt.showSpeeds = _cbSpeeds.isChecked();
        opt.showPowers = _cbPowers.isChecked();
        opt.showAngles = _cbAngles.isChecked();
        opt.showVoltages = _cbVoltages.isChecked();

        // Dinamicki parametri generatora - dostupno i za small i za large.
        if (!_fileGenParams.empty())
            opt.genParams = _fileGenParams;

        if (_isLargeSystem)
        {
            // Cvorovi za prikaz dolaze direktno iz fajla (PLOT red), ne preko GUI-a.
            if (_fileSelectedBuses.size() != 0)
            {
                opt.selectedBuses.reserve(_fileSelectedBuses.size());
                for (size_t i = 0; i < _fileSelectedBuses.size(); ++i)
                    opt.selectedBuses.push_back(_fileSelectedBuses[i]);
            }

            if (_useFileEvents && !_fileLoadedProfiles.empty())
                opt.loadProfiles = _fileLoadedProfiles;
            return opt;
        }

        // Small: fajl poremecaja (ako je uspjesno ucitan) ima prioritet nad
        // rucnim GUI unosom - ali samo za onaj DIO koji fajl stvarno navodi.
        // Ako fajl nema PLOT red, bus selekcija se i dalje uzima iz GUI-a
        // (checkboxovi/range) - fajl bez PLOT-a ne znaci "prikazi sve" u
        // ovom slucaju, nego "koristi ono sto je vec izabrano u GUI-u".
        bool fileEventsActive = _useFileEvents && !_fileLoadedProfiles.empty();

        if (fileEventsActive && _fileSelectedBuses.size() != 0)
        {
            opt.selectedBuses.reserve(_fileSelectedBuses.size());
            for (size_t i = 0; i < _fileSelectedBuses.size(); ++i)
                opt.selectedBuses.push_back(_fileSelectedBuses[i]);
        }
        else
        {
            // Small: bus selekcija (GUI)
            if (_useCheckBoxes)
            {
                if (!_cbAllBuses.isChecked())
                {
                    opt.selectedBuses.reserve(_busCheckBoxes.size());
                    for (size_t i = 0; i < _busCheckBoxes.size(); ++i)
                        if (_busCheckBoxes[i]->isChecked())
                            opt.selectedBuses.push_back(_displayBuses[i].busId);
                }
            }
            else
            {
                td::String rangeStr = _editBusRange.getText();
                if (!rangeStr.isEmpty() && strcmp(rangeStr.c_str(), "all") != 0)
                    parseRangeString(rangeStr, opt.selectedBuses);
            }
        }

        if (fileEventsActive)
        {
            opt.loadProfiles = _fileLoadedProfiles;
            return opt;
        }

        // Small: manual eventi (koristi se samo ako fajl NIJE aktivan)
        if (_useBusRows)
        {
            for (size_t i = 0; i < _busEventRows.size(); ++i)
            {
                if (!_busEventRows[i].pCb->isChecked()) continue;
                float tStart = 0.5f, tEnd = 6.0f, newPd = 0.0f, newQd = 0.0f;
                _busEventRows[i].pTStart->getValue(tStart);
                _busEventRows[i].pTEnd->getValue(tEnd);
                _busEventRows[i].pNewPd->getValue(newPd);
                _busEventRows[i].pNewQd->getValue(newQd);
                float origPd = (float)_displayBuses[i].origPd;
                float origQd = (float)_displayBuses[i].origQd;
                BusLoadProfile profile;
                profile.busId = _busEventRows[i].busId;
                TimePoint pt1; pt1.t = tStart; pt1.Pd = newPd;  pt1.Qd = newQd;
                TimePoint pt2; pt2.t = tEnd;   pt2.Pd = origPd; pt2.Qd = origQd;
                profile.points.push_back(pt1);
                profile.points.push_back(pt2);
                opt.loadProfiles.push_back(profile);
            }
        }
        else
        {
            for (int i = 0; i < MAX_EVENTS; ++i)
            {
                td::INT4 busId = 0;
                _eventRows[i].pBusId->getValue(busId);
                if (busId <= 0) continue;
                float tStart = 0.5f, tEnd = 6.0f, newPd = 0.0f, newQd = 0.0f;
                _eventRows[i].pTStart->getValue(tStart);
                _eventRows[i].pTEnd->getValue(tEnd);
                _eventRows[i].pNewPd->getValue(newPd);
                _eventRows[i].pNewQd->getValue(newQd);
                float origPd = 0.0f, origQd = 0.0f;
                for (size_t j = 0; j < _displayBuses.size(); ++j)
                    if (_displayBuses[j].busId == (int)busId)
                    {
                        origPd = (float)_displayBuses[j].origPd;
                        origQd = (float)_displayBuses[j].origQd;
                        break;
                    }
                BusLoadProfile profile;
                profile.busId = (int)busId;
                TimePoint pt1; pt1.t = tStart; pt1.Pd = newPd;  pt1.Qd = newQd;
                TimePoint pt2; pt2.t = tEnd;   pt2.Pd = origPd; pt2.Qd = origQd;
                profile.points.push_back(pt1);
                profile.points.push_back(pt2);
                opt.loadProfiles.push_back(profile);
            }
        }
        return opt;
    }
};