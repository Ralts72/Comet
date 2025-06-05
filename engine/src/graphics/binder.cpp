#include "binder.h"

namespace Comet {

    BindGroup::BindGroup(BindGroupRes* res) : m_res(res) {}
        
    BindGroup::BindGroup(BindGroup&&) noexcept{}

    BindGroup& BindGroup::operator=(BindGroup&&) noexcept{}

    BindGroup::~BindGroup(){}

    const BindGroup::Descriptor& BindGroup::getDescriptor() const{}
}
