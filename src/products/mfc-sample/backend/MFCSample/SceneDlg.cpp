// SceneDlg.cpp: 实现文件
//

#include "pch.h"
#include "MFCSample.h"
#include "afxdialogex.h"
#include "SceneDlg.h"


// SceneDlg 对话框

IMPLEMENT_DYNAMIC(SceneDlg, CDialogEx)

SceneDlg::SceneDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_DIALOG1, pParent)
{

}

SceneDlg::~SceneDlg()
{
}

void SceneDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(SceneDlg, CDialogEx)
END_MESSAGE_MAP()


// SceneDlg 消息处理程序
