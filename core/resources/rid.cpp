#include "rid.h"

namespace feather {

constexpr RID RID::invalid() {
	return RID { 0 };
}

bool RID::is_valid() const {
	return id != invalid().id;
}

} //namespace feather
