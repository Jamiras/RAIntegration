#include "ModelProperty.hh"

#include "ra_fwd.h"

namespace ra {
namespace data {

// NOTE: These static variables are not guarded by a mutex. All ModelProperties are expected to be defined statically,
// and therefore initialized in a sequential manner when the DLL is loaded. s_vProperties is a vector of _references_,
// but you can't declare a vector of reference types, so it's implemented as a vector of pointers. Don't worry about
// deleting the pointers, they should be pointing to the static ModelProperties, which aren't allocated.
int ModelPropertyBase::s_nNextKey = 1;
std::vector<ModelPropertyBase*> ModelPropertyBase::s_vProperties;

_Use_decl_annotations_ ModelPropertyBase::ModelPropertyBase(const char* const sTypeName,
                                                            const char* const sPropertyName) :
    m_sTypeName{sTypeName},
    m_sPropertyName{sPropertyName}
{
    // ASSERT: ModelProperties are static variables and constructed when the DLL is loaded.
    // We cannot reasonably use a mutex because it cannot be locked during static initialization in WinXP.
    // ASSERT: The vector is implicitly sorted as new items are allocated higher keys and added to the end of the
    // vector.
    m_nKey = s_nNextKey++;

    if (s_vProperties.empty())
    {
        s_vProperties.reserve(128);

        // If you encounter this exception, make sure ModelProperty.cpp appears before any files that use it in the
        // vsproj file. The linker will initialize static objects in the order that they're linked, so any
        // ModelProperty that gets initialized before the ModelPropertyBase class is initialized may be lost.
        Expects(m_nKey == 1);
    }

    s_vProperties.push_back(this);
}

ModelPropertyBase::~ModelPropertyBase() noexcept
{
    auto iter = std::find(s_vProperties.begin(), s_vProperties.end(), this);
    if (iter != s_vProperties.end())
        s_vProperties.erase(iter);
}

const ModelPropertyBase* ModelPropertyBase::GetPropertyForKey(int nKey)
{
    ModelPropertyBase search(nKey);
    auto iter = std::lower_bound(s_vProperties.begin(), s_vProperties.end(), &search,
        [](const ModelPropertyBase* restrict left, const ModelPropertyBase* restrict right)
    {
        if (left == nullptr)
            return (right != nullptr);
        if (right == nullptr)
            return false;
        return left->GetKey() < right->GetKey();
    });

    return (iter == s_vProperties.end() ? nullptr : *iter);
}

} // namespace data
} // namespace ra
