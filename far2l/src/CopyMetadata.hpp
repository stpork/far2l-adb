#pragma once
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#if defined(__APPLE__)
#include <copyfile.h>
#include <sys/acl.h>
#elif defined(__FreeBSD__)
#include <sys/acl.h>
#include <sys/extattr.h>
#elif defined(__NetBSD__)
#include <sys/extattr.h>
#elif defined(__linux__) || defined(__CYGWIN__)
#include <sys/xattr.h>
#endif

// The staged inode has fresh metadata. Restore the access policy and attributes
// that an in-place overwrite of the original inode would have retained.
inline bool PreserveCopyAttributes(int original, int staged, bool keep_mode, bool keep_xattrs)
{
#if defined(__APPLE__)
	if (keep_xattrs && fcopyfile(original, staged, nullptr, COPYFILE_XATTR) != 0)
		return false;
	if (keep_mode) {
		acl_t acl = acl_get_fd(original);
		if (!acl) {
			if (errno == ENOTSUP) return true;
			if (errno != ENOENT) return false;
			// A clone or a child of an ACL-bearing directory can already have an ACL.
			acl = acl_get_fd(staged);
			if (!acl) return errno == ENOENT;
			acl_free(acl);
			acl = acl_init(0);
			if (!acl) return false;
		}
		int rc = acl_set_fd(staged, acl);
		acl_free(acl);
		return rc == 0;
	}
#elif defined(__linux__) || defined(__CYGWIN__)
	if (!keep_mode && !keep_xattrs) return true;
	ssize_t size = flistxattr(original, nullptr, 0);
	if (size < 0) return errno == ENOTSUP;
	std::vector<char> names(size);
	if (size && flistxattr(original, names.data(), names.size()) != size) return false;
	bool has_acl = false;
	for (size_t i = 0; i < names.size();) {
		const char *name = names.data() + i;
		i += strlen(name) + 1;
		bool acl = strcmp(name, "system.posix_acl_access") == 0;
		has_acl |= acl;
		if (!(acl ? keep_mode : keep_xattrs)) continue;
		// These describe or grant privileges to the old executable contents.
		if (!strcmp(name, "security.capability") || !strcmp(name, "security.ima")
				|| !strcmp(name, "security.evm")) continue;
		ssize_t length = fgetxattr(original, name, nullptr, 0);
		if (length < 0) return false;
		std::vector<char> value(length);
		if (fgetxattr(original, name, value.data(), value.size()) != length) return false;
		if (fsetxattr(staged, name, value.data(), value.size(), 0) < 0) {
			// Some system labels can be read but only set by a privileged process.
			std::vector<char> inherited(length);
			if (fgetxattr(staged, name, inherited.data(), inherited.size()) != length
					|| inherited != value) return false;
		}
	}
	if (keep_mode && !has_acl && fremovexattr(staged, "system.posix_acl_access") < 0)
		return errno == ENODATA || errno == ENOTSUP;
#elif defined(__FreeBSD__)
	if (keep_mode) {
		long nfs4 = fpathconf(original, _PC_ACL_NFS4);
		if (nfs4 < 0 && errno != EINVAL) return false;
		long posix = nfs4 > 0 ? 0 : fpathconf(original, _PC_ACL_EXTENDED);
		if (posix < 0 && errno != EINVAL) return false;
		if (nfs4 > 0 || posix > 0) {
			acl_type_t type = nfs4 > 0 ? ACL_TYPE_NFS4 : ACL_TYPE_ACCESS;
			acl_t acl = acl_get_fd_np(original, type);
			if (!acl) return false;
			int rc = acl_set_fd_np(staged, acl, type);
			acl_free(acl);
			if (rc != 0) return false;
		}
	}
	if (keep_xattrs) {
		for (int ns : {EXTATTR_NAMESPACE_USER, EXTATTR_NAMESPACE_SYSTEM}) {
			ssize_t size = extattr_list_fd(original, ns, nullptr, 0);
			if (size < 0) {
				if (errno == ENOTSUP) continue;
				return false;
			}
			std::vector<unsigned char> names(size);
			if (size && extattr_list_fd(original, ns, names.data(), size) != size) return false;
			for (size_t i = 0; i < names.size();) {
				size_t n = names[i++];
				if (n > names.size() - i) return false;
				std::string name(reinterpret_cast<const char *>(names.data() + i), n);
				i += n;
				// The ACL API above handles system ACL records.
				if (ns == EXTATTR_NAMESPACE_SYSTEM && name.rfind("posix1e.", 0) == 0) continue;
				ssize_t len = extattr_get_fd(original, ns, name.c_str(), nullptr, 0);
				if (len < 0) return false;
				std::vector<char> data(len);
				if (extattr_get_fd(original, ns, name.c_str(), data.data(), len) != len
						|| extattr_set_fd(staged, ns, name.c_str(), data.data(), len) != len)
					return false;
			}
		}
	}
#elif defined(__NetBSD__)
	if (keep_xattrs && fcpxattr(original, staged) != 0 && errno != ENOTSUP) return false;
#endif
	return true;
}
