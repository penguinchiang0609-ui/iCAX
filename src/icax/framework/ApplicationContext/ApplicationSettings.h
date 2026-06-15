#pragma once

#include "ApplicationContextExport.h"

#include <Data/PropertyBag.h>

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP CApplicationSettings final
        {
        public:
            CApplicationSettings();
            explicit CApplicationSettings(IN const iCAX::Data::PropertySet& Properties_);
            ~CApplicationSettings();

        public:
            void Set(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);
            void Set(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_);

            iCAX::Data::Variant Get(IN const std::string& strKey_, IN const iCAX::Data::Variant& Default_ = iCAX::Data::Variant()) const;
            iCAX::Data::Variant Get(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Default_ = iCAX::Data::Variant()) const;

            const iCAX::Data::PropertySet& GetProperties() const;

        private:
            iCAX::Data::PropertyBag m_Bag;
        };
    }
}
