#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/LineEdit.h>
#include <gui/TextEdit.h>
#include <gui/HorizontalLayout.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include <fo/FileOperations.h>
#include <gui/FileDialog.h>
#include "WindowCreateModel.h"  // fix: bilo DialogCreateModel.h

class ViewConv : public gui::View
{
protected:
    sc::IPlugin* _pIPlugin;
    sc::IPlugin::CallBack _onComplete;

    gui::Label    _lblInstructions;
    gui::Label    _lblFnIn;
    gui::LineEdit _editFnIn;
    gui::Label    _lblFnOut;
    gui::LineEdit _editFnOut;
    gui::Label    _lblStatus;
    gui::LineEdit _editStatus;
    gui::Button   _btnSelectInFn;
    gui::Button   _btnSelectOutFn;
    gui::TextEdit _te;
    gui::Button   _btnInfo;
    gui::Button   _btnConvert;

    gui::HorizontalLayout _hlButtons;
    gui::GridLayout       _gl;
    td::UINT4             _wndID;

protected:
    // Ucitava fajl preko getAllBusInfo() samo radi trenutne provjere/prikaza
    // osnovnih statistika (broj cvorova/generatora) - ne mijenja nista drugo.
    void previewInputFileStats(const td::String& fileName)
    {
        cnt::PushBackVector<int>     genBuses;
        cnt::PushBackVector<BusInfo> allBuses;
        if (getAllBusInfo(fileName, genBuses, allBuses, _editStatus))
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "INFO! Fajl OK: %d cvorova, %d generatora.",
                (int)allBuses.size(), (int)genBuses.size());
            _editStatus = buf;
            _editStatus.setTextColor(td::Accent::Success);
        }
        else
        {
            _editStatus.setTextColor(td::Accent::Error);
        }
    }

    void handleUserActions()
    {
        _btnInfo.onClick([this] {
            td::String fileName = _editFnIn.getText();
            if (!fo::fileExists(fileName)) { _editStatus = "ERROR! File doesn't exist!"; _editStatus.setTextColor(td::Accent::Error); return; }
            td::String content;
            if (!fo::loadBinaryFile(fileName, content)) { _editStatus = "ERROR! Cannot load file!"; _editStatus.setTextColor(td::Accent::Error); return; }
            _editStatus = "INFO! Content ok!";
            _editStatus.setTextColor(td::Accent::Success);
            _te.setText(content);
            });

        _btnSelectInFn.onClick([this] {
            gui::OpenFileDialog::show(this, tr("openEQModel"), "*.m", _wndID + 1000, [this](gui::FileDialog* pDlg) {
                if (pDlg->getStatus() == gui::FileDialog::Status::OK)
                {
                    td::String fn = pDlg->getFileName();
                    if (!fn.isEmpty())
                    {
                        _editFnIn = fn;
                        _editFnIn.setFocus();
                        // Ideja #5: trenutna provjera/prikaz statistika fajla
                        // odmah nakon izbora, prije klika na Info/Convert.
                        previewInputFileStats(fn);
                    }
                }
                });
            });

        _btnSelectOutFn.onClick([this] {
            gui::SaveFileDialog::show(this, tr("createDmodl"), "*.dmodl", _wndID + 2000, [this](gui::FileDialog* pDlg) {
                if (pDlg->getStatus() == gui::FileDialog::Status::OK)
                {
                    td::String fn = pDlg->getFileName();
                    if (!fn.isEmpty()) { _editFnOut = fn; _editFnOut.setFocus(); }
                }
                });
            });

        _btnConvert.onClick([this] {
            td::String inputFileName = _editFnIn.getText();
            if (inputFileName.isEmpty()) { _editStatus = "ERROR! Empty input file name"; _editStatus.setTextColor(td::Accent::Error); return; }
            if (!fo::fileExists(inputFileName)) { _editStatus = "ERROR! Input file doesn't exist"; _editStatus.setTextColor(td::Accent::Error); return; }
            td::String outFileName = _editFnOut.getText();
            if (outFileName.isEmpty()) { _editStatus = "ERROR! Empty output file name"; _editStatus.setTextColor(td::Accent::Error); return; }

            cnt::PushBackVector<int>     genBuses;
            cnt::PushBackVector<BusInfo> allBuses;
            if (!getAllBusInfo(inputFileName, genBuses, allBuses, _editStatus))
            {
                _editStatus.setTextColor(td::Accent::Error);
                return;
            }

            td::UINT4 wndID = _wndID + 3000;
            auto pParentWnd = getParentWindow();
            auto existing = pParentWnd->getAttachedWindow(wndID);
            if (existing) { existing->setFocus(); return; }

            auto pWnd = new WindowCreateModel(pParentWnd, _pIPlugin,
                inputFileName, outFileName,
                _onComplete, genBuses, allBuses, wndID);
            pWnd->open();
            });
    }

    ViewConv() = delete;

public:
    ViewConv(sc::IPlugin* pIPlugin, const sc::IPlugin::CallBack& onComplete, td::UINT4 wndID = 0)
        : _pIPlugin(pIPlugin)
        , _onComplete(onComplete)
        , _lblInstructions(tr("1. Izaberite Matpower (.m) file -> 2. Izaberite izlaznu putanju (.dmodl) -> 3. Convert (Otvara prozor sa opcijama modela)"))
        , _lblFnIn(tr("Matpower Case file name:"))
        , _lblFnOut(tr("Output file name:"))
        , _lblStatus(tr("Status:"))
        , _btnSelectInFn("…")
        , _btnSelectOutFn("…")
        , _btnInfo(tr("Info"))
        , _btnConvert(tr("Convert"))
        , _hlButtons(3)
        , _gl(6, 3)
        , _wndID(wndID)
    {
        assert(_pIPlugin);
        _editStatus.setAsReadOnly();
        _te.setAsReadOnly();
        _editFnIn.setToolTip(tr("PLEQ_EnterFN"));
        _editFnOut.setToolTip("Putanja gdje ce biti sačuvan izlazni .dmodl fajl");

        // Ideja #1: placeholder tekst dok se ne ucita stvarni sadrzaj fajla
        // (klikom na "Info"), da prazan prostor ne izgleda kao greska.
        _te.setText("Sadržaj Matpower (.m) fajla će se prikazati ovdje nakon klika na 'Info'.");
        _lblInstructions.setBold();
        _te.setBold();
        _lblInstructions.setResizable(40, td::TextEllipsize::End);
        _lblInstructions.setFont(gui::Font::ID::SystemSmallerItalic);
        _lblFnIn.setFont(gui::Font::ID::SystemLargerBold);
        _lblFnOut.setFont(gui::Font::ID::SystemLargerBold);
        _lblStatus.setFont(gui::Font::ID::SystemLargerBold);
        _btnConvert.setFont(gui::Font::ID::SystemLargerBold);

        gui::GridComposer gc(_gl);
        _gl.setMargins(10, 10);
        gc.appendRow(_lblInstructions, 0);
        gc.appendRow(_lblFnIn) << _editFnIn << _btnSelectInFn;
        gc.appendRow(_lblFnOut) << _editFnOut << _btnSelectOutFn;
        gc.appendRow(_lblStatus); gc.appendCol(_editStatus, 0);
        gc.appendRow(_te, 0);
        _hlButtons.appendSpacer() << _btnInfo << _btnConvert;
        gc.appendRow(_hlButtons, 0);

        setLayout(&_gl);
        handleUserActions();
    }

    td::String getOutFileName() const { return _editFnOut.getText(); }
};