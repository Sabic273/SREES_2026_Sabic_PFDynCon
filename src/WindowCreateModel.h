#pragma once
#include <gui/Window.h>
#include "ViewCreateModel.h"

class WindowCreateModel : public gui::Window
{
    ViewCreateModel _view;
public:
    WindowCreateModel(gui::Window* pParent,
        sc::IPlugin* pIPlugin,
        const td::String& inputFileName,
        const td::String& outFileName,
        const sc::IPlugin::CallBack& onComplete,
        const cnt::PushBackVector<int>& genBuses,
        const cnt::PushBackVector<BusInfo>& allBuses,
        td::UINT4 wndID = 0)
        : gui::Window(gui::Size(1000, 800), pParent, wndID)
        , _view(pIPlugin, inputFileName, outFileName, onComplete, allBuses, wndID)
    {
        setTitle("Create Model");
        setResizable(true);
        setCentralView(&_view);
    }
};