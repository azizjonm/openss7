/*****************************************************************************

 @(#) File: src/kernel/strattach.c

 -----------------------------------------------------------------------------

 Copyright (c) 2008-2015  Monavacon Limited <http://www.monavacon.com/>
 Copyright (c) 2001-2008  OpenSS7 Corporation <http://www.openss7.com/>
 Copyright (c) 1997-2001  Brian F. G. Bidulock <bidulock@openss7.org>

 All Rights Reserved.

 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU Affero General Public License as published by the Free
 Software Foundation, version 3 of the license.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more
 details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>, or
 write to the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 02139, USA.

 -----------------------------------------------------------------------------

 U.S. GOVERNMENT RESTRICTED RIGHTS.  If you are licensing this Software on
 behalf of the U.S. Government ("Government"), the following provisions apply
 to you.  If the Software is supplied by the Department of Defense ("DoD"), it
 is classified as "Commercial Computer Software" under paragraph 252.227-7014
 of the DoD Supplement to the Federal Acquisition Regulations ("DFARS") (or any
 successor regulations) and the Government is acquiring only the license rights
 granted herein (the license rights customarily provided to non-Government
 users).  If the Software is supplied to any unit or agency of the Government
 other than DoD, it is classified as "Restricted Computer Software" and the
 Government's rights in the Software are defined in paragraph 52.227-19 of the
 Federal Acquisition Regulations ("FAR") (or any successor regulations) or, in
 the cases of NASA, in paragraph 18.52.227-86 of the NASA Supplement to the FAR
 (or any successor regulations).

 -----------------------------------------------------------------------------

 Commercial licensing and support of this software is available from OpenSS7
 Corporation at a fee.  See http://www.openss7.com/

 *****************************************************************************/

static char const ident[] = "src/kernel/strattach.c (" PACKAGE_ENVR ") " PACKAGE_DATE;

#ifdef NEED_LINUX_AUTOCONF_H
#include NEED_LINUX_AUTOCONF_H
#endif
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/slab.h>
#ifdef HAVE_KINK_LINUX_SMP_LOCK_H
#include <linux/smp_lock.h>
#endif
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/acct.h>
#ifdef HAVE_KINC_LINUX_DEVFS_FS_KERNEL_H
#include <linux/devfs_fs_kernel.h>
#endif
#include <linux/fs.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

#include <linux/seq_file.h>
#ifdef HAVE_KINC_LINUX_NAMESPACE_H
#include <linux/namespace.h>
#endif
#include <linux/file.h>
#ifdef HAVE_KINC_LINUX_NAMEI_H
#include <linux/namei.h>
#endif
#ifdef HAVE_KINC_LINUX_PATH_H
#include <linux/path.h>
#endif
#ifdef HAVE_KINC_LINUX_NSPROXY_H
#include <linux/nsproxy.h>
#endif
#if defined HAVE_KINC_LINUX_SECURITY_H
#include <linux/security.h>	/* avoid ptrace conflict */
#endif

#include "sys/strdebug.h"

#include "sys/config.h"
#include "strattach.h"		/* header verification */

#if defined HAVE_KERNEL_FATTACH_SUPPORT && \
    (!defined CONFIG_KERNEL_WEAK_MODULES || ( \
     ((!defined HAVE_NAMESPACE_SEM_SYMBOL && \
       (!defined HAVE_MOUNT_SEM_SYMBOL || defined HAVE_MOUNT_SEM_EXPORT) ) || \
      defined HAVE_NAMESPACE_SEM_SYMBOL) && \
     defined HAVE_CLONE_MNT_EXPORT && defined HAVE_GRAFT_TREE_EXPORT && defined HAVE_DO_UMOUNT_EXPORT))

#ifdef HAVE_CHECK_MNT_SYMBOL
int check_mnt(struct vfsmount *mnt);
#else
STATIC int
check_mnt(struct vfsmount *mnt)
{
#if defined HAVE_KMEMB_STRUCT_VFSMOUNT_MNT_NS
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
#elif defined HAVE_KMEMB_STRUCT_VFSMOUNT_MNT_NAMESPACE
	return mnt->mnt_namespace == current->namespace;
#else
#error One of vfsmount.mnt_ns or vfsmount.mnt_namepsace must exist.
#endif
}
#endif

#ifdef HAVE_KINC_LINUX_PATH_H
int graft_tree(struct vfsmount *mnt, struct path *nd);
#else
int graft_tree(struct vfsmount *mnt, struct nameidata *nd);
#endif

int do_umount(struct vfsmount *mnt, int flags);

#ifndef HAVE_PATH_LOOKUP_EXPORT
int
path_lookup(const char *path, unsigned flags, struct nameidata *nd)
{
	int error = 0;

	if (path_init(path, flags, nd))
		error = path_walk(path, nd);
	return error;
}
#endif

struct vfsmount *clone_mnt(struct vfsmount *old, struct dentry *root);

streams_fastcall long
do_fattach(const struct file *file, const char *file_name)
{
	/* very much like do_add_mount() but with different permissions and clone_mnt() of the file 
	   instead of do_kern_mount() of the filesystem root */
#ifdef HAVE_KINC_LINUX_PATH_H
	struct path nd;
#else
	struct nameidata nd;
#endif
	struct vfsmount *mnt;
	int err;
#ifdef LOOKUP_POSITIVE
	const unsigned int flags = LOOKUP_FOLLOW | LOOKUP_POSITIVE;
#else
	const unsigned int flags = LOOKUP_FOLLOW;
#endif

	err = -EINVAL;
	if (file->f_dentry->d_sb->s_magic != SPECFS_MAGIC)
		goto out;	/* not a STREAM */
	err = -ENOENT;
	if (!file_name || !*file_name)
		goto out;

#ifdef HAVE_KINC_LINUX_PATH_H
	err = kern_path(file_name, flags, &nd);
#else
	err = path_lookup(file_name, flags, &nd);
#endif
	if (err)
		goto out;

	err = -EPERM;
	/* the owner of the file is permitted to fattach */
	if (!capable(CAP_SYS_ADMIN) && current_creds->cr_uid != file->f_dentry->d_inode->i_uid)
		goto release;

	err = -ENOMEM;
	mnt = clone_mnt(file->f_vfsmnt, file->f_dentry);
	if (!mnt)
		goto release;

#ifdef HAVE_NAMESPACE_SEM_SYMBOL
	down_write(&namespace_sem);
#else
#ifdef HAVE_MOUNT_SEM_SYMBOL
	down(&mount_sem);
#else
	down_write(&current->namespace->sem);
#endif
#endif

	/* Something was mounted here while we slept */
#ifdef HAVE_KINC_LINUX_PATH_H
	while (d_mountpoint(nd.dentry) && follow_down(&nd)) ;
#else
	while (d_mountpoint(nd.dentry) && follow_down(&nd.mnt, &nd.dentry)) ;
#endif

	err = -EINVAL;
	if (!check_mnt(nd.mnt))
		goto unlock;

	/* Refuse the same filesystem on the same mount point */
	err = -EBUSY;
	if (nd.mnt->mnt_sb == mnt->mnt_sb && nd.mnt->mnt_root == nd.dentry)
		goto unlock;

	mnt->mnt_flags = 0;
	err = graft_tree(mnt, &nd);

      unlock:
#ifdef HAVE_NAMESPACE_SEM_SYMBOL
	up_write(&namespace_sem);
#else
#ifdef HAVE_MOUNT_SEM_SYMBOL
	up(&mount_sem);
#else
	up_write(&current->namespace->sem);
#endif
#endif
	mntput(mnt);
      release:
#ifdef HAVE_KINC_LINUX_PATH_H
	path_put(&nd);
#else
	path_release(&nd);
#endif
      out:
	return err;
}

EXPORT_SYMBOL_GPL(do_fattach);

streams_fastcall long
do_fdetach(const char *file_name)
{
	/* pretty much the same as sys_umount() with different permissions */
#ifdef HAVE_KINC_LINUX_PATH_H
	struct path nd;
#else
	struct nameidata nd;
#endif
	int err;
#ifdef LOOKUP_POSITIVE
	unsigned int flags = LOOKUP_FOLLOW | LOOKUP_POSITIVE;
#else
	unsigned int flags = LOOKUP_FOLLOW;
#endif

	err = -ENOENT;
	if (!file_name || !*file_name)
		goto out;

#ifdef HAVE_KINC_LINUX_PATH_H
	err = kern_path(file_name, flags, &nd);
#else
	err = path_lookup(file_name, flags, &nd);
#endif
	if (err)
		goto out;

	err = -EINVAL;
	if (nd.dentry != nd.mnt->mnt_root)
		goto release;	/* not mounted */
	if (!check_mnt(nd.mnt))
		goto release;
	if (nd.dentry->d_sb->s_magic != SPECFS_MAGIC)
		goto release;	/* not a STREAM */

	err = -EPERM;
	/* the owner of the (real) file is permitted to fdetach */
	if (!capable(CAP_SYS_ADMIN) && current_creds->cr_uid != nd.mnt->mnt_mountpoint->d_inode->i_uid)
		goto release;

	err = do_umount(nd.mnt, MNT_DETACH);

      release:
#ifdef HAVE_KINC_LINUX_PATH_H
	path_put(&nd);
#else
	path_release(&nd);
#endif
      out:
	return err;
}

EXPORT_SYMBOL_GPL(do_fdetach);

#endif				/* defined HAVE_KERNEL_FATTACH_SUPPORT */
