#ifndef WEBSERV_COMMON_FILE_DESCRIPTOR_HPP
#define WEBSERV_COMMON_FILE_DESCRIPTOR_HPP

namespace ws {

class FileDescriptor {
public:
	explicit FileDescriptor(int fd = -1);
	~FileDescriptor();

	int  get() const;
	int  release();
	void reset(int fd = -1);
	bool valid() const;

private:
	int fd_;

	FileDescriptor(const FileDescriptor&);
	FileDescriptor& operator=(const FileDescriptor&);
};

}

#endif
