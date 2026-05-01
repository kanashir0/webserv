#include "common/FileDescriptor.hpp"
#include <unistd.h>

namespace ws {

FileDescriptor::FileDescriptor(int fd) : fd_(fd) {}

FileDescriptor::~FileDescriptor() {
	if (fd_ >= 0) ::close(fd_);
}

int FileDescriptor::get() const { return fd_; }

int FileDescriptor::release() {
	int tmp = fd_;
	fd_ = -1;
	return tmp;
}

void FileDescriptor::reset(int fd) {
	if (fd_ >= 0 && fd_ != fd) ::close(fd_);
	fd_ = fd;
}

bool FileDescriptor::valid() const { return fd_ >= 0; }

}
