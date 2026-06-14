#include "pch.h"
#include "NcAux.h"

//!< 构造函数
iCAX::NcData::ncAux::ncAux()
    : nType(kAuxComment)
    , Comment()
{
}

//!< 构造函数
iCAX::NcData::ncAux::ncAux(IN const ncAuxComment& Comment_)
    : nType(kAuxComment)
    , Comment(Comment_)
{
}

//!< 构造函数
iCAX::NcData::ncAux::ncAux(IN const ncAuxCommand& Command_)
    : nType(kAuxComment)
    , Command(Command_)
{
}

//!< 拷贝构造函数
iCAX::NcData::ncAux::ncAux(IN const ncAux& Right_)
    : nType(Right_.nType)
    , Comment()
{
    switch (nType)
    {
    case iCAX::NcData::ncAux::kAuxComment:
        Comment = Right_.Comment;
        break;
    case iCAX::NcData::ncAux::kAuxCommand:
        Command = Right_.Command;
        break;
    default:
        break;
    }
}

//!< 赋值构造函数
iCAX::NcData::ncAux& iCAX::NcData::ncAux::operator=(IN const ncAux& Right_)
{
    switch (nType)
    {
    case iCAX::NcData::ncAux::kAuxComment:
        Comment = {};
        break;
    case iCAX::NcData::ncAux::kAuxCommand:
        Command = {};
        break;
    default:
        break;
    }

    nType = Right_.nType;
    switch (nType)
    {
    case iCAX::NcData::ncAux::kAuxComment:
        Comment = Right_.Comment;
        break;
    case iCAX::NcData::ncAux::kAuxCommand:
        Command = Right_.Command;
        break;
    default:
        break;
    }

    return *this;
}

//!< 析构函数
iCAX::NcData::ncAux::~ncAux()
{
    switch (nType)
    {
    case iCAX::NcData::ncAux::kAuxComment:
        Comment = {};
        break;
    case iCAX::NcData::ncAux::kAuxCommand:
        Command = {};
        break;
    default:
        break;
    }
}


