#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace iCAX::ProjectFile::detail
{
    class CSavePerformance final
    {
    public:
        explicit CSavePerformance(const char* scope) : m_Scope(scope)
        {
            char* value=nullptr;std::size_t length=0;
            (void)_dupenv_s(&value,&length,"ICAX_PROFILE_PROJECT_SAVE");
            m_Enabled=value!=nullptr;std::free(value);
        }
        void Mark(const char* stage) const
        {
            if(m_Enabled)std::fprintf(stderr,"[project-save] %s/%s %.6f sec\n",m_Scope,stage,
                std::chrono::duration<double>(std::chrono::steady_clock::now()-m_Start).count());
        }
    private:
        const char* m_Scope;
        bool m_Enabled=false;
        const std::chrono::steady_clock::time_point m_Start=std::chrono::steady_clock::now();
    };
}
