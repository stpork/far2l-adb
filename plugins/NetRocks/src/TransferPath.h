#pragma once
#include <string>
#include <vector>

// Lexical operations only; filesystem identity checks belong to the provider.
inline std::vector<std::string> TransferPathParts(const std::string &path)
{
	std::vector<std::string> parts;
	for (size_t start = 0; start < path.size();) {
		size_t end = path.find('/', start);
		if (end == std::string::npos) end = path.size();
		std::string part = path.substr(start, end - start);
		if (part == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
		else if (!part.empty() && part != "." && (part != ".." || path[0] != '/')) parts.push_back(part);
		start = end + 1;
	}
	return parts;
}

inline std::string NormalizeTransferPath(const std::string &path)
{
	std::string out = !path.empty() && path[0] == '/' ? "/" : "";
	for (const auto &part : TransferPathParts(path)) {
		if (!out.empty() && out.back() != '/') out+= '/';
		out+= part;
	}
	return out.empty() ? "." : out;
}

inline std::string RelativeTransferTarget(const std::string &link, const std::string &target)
{
	auto parent = TransferPathParts(link);
	if (!parent.empty()) parent.pop_back();
	const auto dest = TransferPathParts(target);
	size_t common = 0;
	while (common < parent.size() && common < dest.size() && parent[common] == dest[common]) ++common;
	std::string out;
	for (size_t i = common; i < parent.size(); ++i) out+= "../";
	for (size_t i = common; i < dest.size(); ++i) {
		if (!out.empty() && out.back() != '/') out+= '/';
		out+= dest[i];
	}
	return out.empty() ? "." : out;
}
