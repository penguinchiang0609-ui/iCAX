
// MFCSample.h: PROJECT_NAME 应用程序的主头文件
//

#pragma once

#include "Engine/CAXEngine.h"
#include <memory>

#ifndef __AFXWIN_H__
	#error "在包含此文件之前包含 'pch.h' 以生成 PCH"
#endif

#include "resource.h"		// 主符号


// CMFCSampleApp:
// 有关此类的实现，请参阅 MFCSample.cpp
//

class CMFCSampleApp : public CWinApp
{
public:
	CMFCSampleApp();

// 重写
public:
	virtual BOOL InitInstance() override;
    virtual int ExitInstance() override;
// 实现

	DECLARE_MESSAGE_MAP()

private:
    std::shared_ptr<iCAX::Engine::CCAXEngine> m_pAppEngine;
};

extern CMFCSampleApp theApp;
