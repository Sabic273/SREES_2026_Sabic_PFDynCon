#pragma once
#include <gui/StandardTabView.h>
#include "ViewConv.h"
#include "ViewHelp.h"

class TabView : public gui::StandardTabView
{
protected:
    ViewConv _v1;
    ViewHelp _v2;
    TabView() = delete;
public:
    TabView(sc::IPlugin* pIPlugin, const sc::IPlugin::CallBack& onComplete, td::UINT4 wndID = 0)
        : _v1(pIPlugin, onComplete, wndID)
    {
        addView(&_v1, tr("Converter"));
        addView(&_v2, tr("About"));
    }

    td::String getOutFileName() const
    {
        return _v1.getOutFileName();
    }
};