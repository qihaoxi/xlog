/* =====================================================================================
 *       Filename:  platform.c
 *    Description:  Cross-platform compatibility layer implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

/* ============================================================================
 * File System Operations
 * ============================================================================ */
#ifdef XLOG_PLATFORM_WINDOWS

#include <sys/stat.h>

int64_t xlog_file_size(const char *path)
{
	struct _stat64 st;
	if (_stat64(path, &st) != 0)
	{
		return -1;
	}
	return (int64_t)st.st_size;
}

bool xlog_file_exists(const char *path)
{
	return _access(path, 0) == 0;
}

bool xlog_is_directory(const char *path)
{
	struct _stat64 st;
	if (_stat64(path, &st) != 0)
	{
		return false;
	}
	return (st.st_mode & _S_IFDIR) != 0;
}

bool xlog_mkdir_p(const char *path)
{
	char tmp[1024];
	char *p = NULL;
	size_t len;

	xlog_snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);

	/* Remove trailing separator */
	if (tmp[len - 1] == '\\' || tmp[len - 1] == '/')
	{
		tmp[len - 1] = '\0';
	}

	for (p = tmp + 1; *p; p++)
	{
		if (*p == '\\' || *p == '/')
		{
			*p = '\0';
			if (!xlog_file_exists(tmp))
			{
				if (_mkdir(tmp) != 0 && errno != EEXIST)
				{
					return false;
				}
			}
			*p = '\\';
		}
	}

	if (!xlog_file_exists(tmp))
	{
		return _mkdir(tmp) == 0;
	}
	return true;
}

bool xlog_rename(const char *old_path, const char *new_path)
{
	/* Windows MoveFileEx can overwrite */
	return MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING) != 0;
}

bool xlog_remove(const char *path)
{
	return DeleteFileA(path) != 0;
}

int64_t xlog_dir_free_space(const char *path)
{
	ULARGE_INTEGER free_bytes;
	if (GetDiskFreeSpaceExA(path, &free_bytes, NULL, NULL))
	{
		return (int64_t)free_bytes.QuadPart;
	}
	return -1;
}

int64_t xlog_dir_used_space(const char *dir_path, const char *pattern)
{
	char search_path[1024];
	WIN32_FIND_DATAA find_data;
	HANDLE hFind;
	int64_t total_size = 0;

	xlog_snprintf(search_path, sizeof(search_path), "%s\\%s", dir_path, pattern);

	hFind = FindFirstFileA(search_path, &find_data);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	do
	{
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			LARGE_INTEGER file_size;
			file_size.LowPart = find_data.nFileSizeLow;
			file_size.HighPart = find_data.nFileSizeHigh;
			total_size += file_size.QuadPart;
		}
	} while (FindNextFileA(hFind, &find_data));

	FindClose(hFind);
	return total_size;
}

int xlog_list_files(const char *dir_path, const char *pattern,
					xlog_dir_callback callback, void *user_data)
{
	char search_path[1024];
	WIN32_FIND_DATAA find_data;
	HANDLE hFind;
	int count = 0;

	xlog_snprintf(search_path, sizeof(search_path), "%s\\%s", dir_path, pattern);

	hFind = FindFirstFileA(search_path, &find_data);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	do
	{
		if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (callback)
			{
				callback(find_data.cFileName, user_data);
			}
			count++;
		}
	} while (FindNextFileA(hFind, &find_data));

	FindClose(hFind);
	return count;
}

/* Windows Thread Support */
typedef struct
{
	void *(*func)(void *);
	void *arg;
} win_thread_wrapper_t;

static DWORD WINAPI win_thread_func(LPVOID arg)
{
	win_thread_wrapper_t *wrapper = (win_thread_wrapper_t *)arg;
	void *(*func)(void *) = wrapper->func;
	void *func_arg = wrapper->arg;
	free(wrapper);
	func(func_arg);
	return 0;
}

int xlog_thread_create(xlog_thread_t *thread, void *(*func)(void *), void *arg)
{
	win_thread_wrapper_t *wrapper = malloc(sizeof(win_thread_wrapper_t));
	if (!wrapper)
		return -1;
	wrapper->func = func;
	wrapper->arg = arg;

	*thread = CreateThread(NULL, 0, win_thread_func, wrapper, 0, NULL);
	if (*thread == NULL)
	{
		free(wrapper);
		return -1;
	}
	return 0;
}

int xlog_thread_join(xlog_thread_t thread, void **retval)
{
	(void)retval;
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
	return 0;
}

int xlog_mutex_init(xlog_mutex_t *mutex)
{
	InitializeCriticalSection(mutex);
	return 0;
}

int xlog_mutex_destroy(xlog_mutex_t *mutex)
{
	DeleteCriticalSection(mutex);
	return 0;
}

int xlog_mutex_lock(xlog_mutex_t *mutex)
{
	EnterCriticalSection(mutex);
	return 0;
}

int xlog_mutex_unlock(xlog_mutex_t *mutex)
{
	LeaveCriticalSection(mutex);
	return 0;
}

int xlog_cond_init(xlog_cond_t *cond)
{
	InitializeConditionVariable(cond);
	return 0;
}

int xlog_cond_destroy(xlog_cond_t *cond)
{
	(void)cond;
	return 0;
}

int xlog_cond_wait(xlog_cond_t *cond, xlog_mutex_t *mutex)
{
	SleepConditionVariableCS(cond, mutex, INFINITE);
	return 0;
}

int xlog_cond_timedwait(xlog_cond_t *cond, xlog_mutex_t *mutex, uint32_t timeout_ms)
{
	if (!SleepConditionVariableCS(cond, mutex, timeout_ms))
	{
		if (GetLastError() == ERROR_TIMEOUT)
		{
			return XLOG_ETIMEDOUT;
		}
		return -1;
	}
	return 0;
}

int xlog_cond_signal(xlog_cond_t *cond)
{
	WakeConditionVariable(cond);
	return 0;
}

int xlog_cond_broadcast(xlog_cond_t *cond)
{
	WakeAllConditionVariable(cond);
	return 0;
}

#else /* POSIX */

#include <fnmatch.h>
#include <sys/statvfs.h>

int64_t xlog_file_size(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
	{
		return -1;
	}
	return (int64_t) st.st_size;
}

bool xlog_file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

bool xlog_is_directory(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
	{
		return false;
	}
	return S_ISDIR(st.st_mode);
}

bool xlog_mkdir_p(const char *path)
{
	char tmp[1024];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);

	/* Remove trailing separator */
	if (tmp[len - 1] == '/')
	{
		tmp[len - 1] = '\0';
	}

	for (p = tmp + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			if (!xlog_file_exists(tmp))
			{
				if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
				{
					return false;
				}
			}
			*p = '/';
		}
	}

	if (!xlog_file_exists(tmp))
	{
		return mkdir(tmp, 0755) == 0;
	}
	return true;
}

bool xlog_rename(const char *old_path, const char *new_path)
{
	return rename(old_path, new_path) == 0;
}

bool xlog_remove(const char *path)
{
	return unlink(path) == 0;
}

int64_t xlog_dir_free_space(const char *path)
{
	struct statvfs st;
	if (statvfs(path, &st) != 0)
	{
		return -1;
	}
	return (int64_t) st.f_bavail * (int64_t) st.f_frsize;
}

int64_t xlog_dir_used_space(const char *dir_path, const char *pattern)
{
	DIR *dir;
	struct dirent *entry;
	int64_t total_size = 0;
	char filepath[1024];

	dir = opendir(dir_path);
	if (!dir)
	{
		return 0;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
		{
			if (fnmatch(pattern, entry->d_name, 0) == 0)
			{
				snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
				int64_t size = xlog_file_size(filepath);
				if (size > 0)
				{
					total_size += size;
				}
			}
		}
	}

	closedir(dir);
	return total_size;
}

int xlog_list_files(const char *dir_path, const char *pattern,
                    xlog_dir_callback callback, void *user_data)
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	dir = opendir(dir_path);
	if (!dir)
	{
		return 0;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
		{
			if (fnmatch(pattern, entry->d_name, 0) == 0)
			{
				if (callback)
				{
					callback(entry->d_name, user_data);
				}
				count++;
			}
		}
	}

	closedir(dir);
	return count;
}

#endif /* XLOG_PLATFORM_WINDOWS */

