#pragma once
#include <string>
#include "Version.h"
/*
* @brief 项目描述
*/
class CProjectDescription
{
    std::string strProjectName;                 //!< 项目名称
    std::string strAuthor;                      //!< 作者名称
    std::string strCompany;                     //!< 公司
    std::string strCreateTime;                  //!< 创建日期
    std::string strLastModified;                //!< 上次修改日期
    Version FileVersion;                        //!< 文件版本号
    std::string strAppName;                     //!< 软件名称
    Version AppVersion;                         //!< 软件版本号
    std::string Description;                    //!< 其他描述
    std::string strPwd;                         //!< 项目密码
};