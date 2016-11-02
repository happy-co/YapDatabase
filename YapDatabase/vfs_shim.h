// Define this symbol when building to include a shim on SQLite's VFS that allows you to cause writes to the database to fail
// simply by changing the value of a global variable (see vfs_shim.c for info)
#ifdef HPY_SQLITE_VFS_SHIM

#ifndef vfs_shim_h
#define vfs_shim_h


int RegisterVFSShim(void);


#endif //vfs_shim_h

#endif //HPY_SQLITE_VFS_SHIM
