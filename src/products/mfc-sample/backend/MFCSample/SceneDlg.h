#pragma once
#include "afxdialogex.h"


// SceneDlg 对话框

class SceneDlg : public CDialogEx
{
	DECLARE_DYNAMIC(SceneDlg)

public:
	SceneDlg(CWnd* pParent = nullptr);   // 标准构造函数
	virtual ~SceneDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG1 };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

	DECLARE_MESSAGE_MAP()
};
