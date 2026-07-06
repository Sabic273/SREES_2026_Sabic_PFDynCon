#pragma once
#include <gui/Window.h>
#include "TabView.h"

class WindowPlugin : public gui::Window
{
protected:
    TabView _tabView;
    sc::IPlugin::Cleaner _cleanPlugin;

    void onClose() override final
    {
        _cleanPlugin();
        onClosedPluginWindow();
    }

public:
    WindowPlugin(gui::Window* parentWnd, sc::IPlugin* pIPlugin, const sc::IPlugin::CallBack& onComplete, const sc::IPlugin::Cleaner& cleaner, td::UINT4 wndID = 0)
        : gui::Window(gui::Size(800, 600), parentWnd, wndID)
        , _tabView(pIPlugin, onComplete, wndID)
        , _cleanPlugin(cleaner)
    {
        setTitle(tr("Power Flow Plugin"));
        setCentralView(&_tabView);
    }

    td::String getOutFileName() const
    {
        return _tabView.getOutFileName();
    }
};