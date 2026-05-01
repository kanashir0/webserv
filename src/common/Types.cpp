#include "common/Types.hpp"
#include <cctype>

namespace ws {

bool CaseInsensitiveLess::operator()(const std::string& a, const std::string& b) const {
	std::string::size_type i = 0;
	while (i < a.size() && i < b.size()) {
		unsigned char ca = static_cast<unsigned char>(std::tolower(a[i]));
		unsigned char cb = static_cast<unsigned char>(std::tolower(b[i]));
		if (ca != cb) return ca < cb;
		++i;
	}
	return a.size() < b.size();
}

}
