"""
Copyright (c) 2017-2019 ARM Limited. All rights reserved.

SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""

import os
import json
import sys
import subprocess
import logging
import argparse
from os.path import dirname, abspath, join, isfile, normpath

# Be sure that the tools directory is in the search path
ROOT = abspath(join(dirname(__file__), os.path.pardir, os.path.pardir))
sys.path.insert(0, ROOT)

from tools.utils import delete_dir_files, mkdir, copy_file


def del_file(name):
    """ Delete the file in RTOS/CMSIS/features directory of mbed-os
    Args:
    name - name of the file
    """
    result = []
    search_path = [join(ROOT, 'rtos'), join(ROOT, 'cmsis'),
                   join(ROOT, 'features')]
    for path in search_path:
        for root, dirs, files in os.walk(path):
            if name in files:
                result.append(join(root, name))
    for f in result:
        os.remove(f)
        rel_log.debug("Deleted %s", os.path.relpath(file, ROOT))


def copy_folder(src, dest):
    """ Copy contents of folder in mbed-os listed path
    Args:
    src - src folder path
    dest - destination folder path
    """
    files = os.listdir(src)
    for f in files:
        abs_src_file = join(src, f)
        if os.path.isfile(abs_src_file):
            abs_dst_file = join(dest, f)
            mkdir(dirname(abs_dst_file))
            copy_file(abs_src_file, abs_dst_file)


def run_cmd_with_output(command, exit_on_failure=False):
    """ Passes a command to the system and returns a True/False result once the
        command has been executed, indicating success/failure. If the command was
        successful then the output from the command is returned to the caller.
        Commands are passed as a list of tokens.
        E.g. The command 'git remote -v' would be passed in as ['git', 'remote', '-v']

    Args:
    command - system command as a list of tokens
    exit_on_failure - If True exit the program on failure (default = False)

    Returns:
    result - True/False indicating the success/failure of the command
    output - The output of the command if it was successful, else empty string
    """
    rel_log.debug('[Exec] %s', ' '.join(command))
    returncode = 0
    output = ""
    try:
        output = subprocess.check_output(command)
    except subprocess.CalledProcessError as e:
        returncode = e.returncode

        if exit_on_failure:
            rel_log.error("The command %s failed with return code: %s",
                          (' '.join(command)), returncode)
            sys.exit(1)
    return returncode, output


def get_curr_sha(repo_path):
    """ Gets the latest SHA for the specified repo
    Args:
    repo_path - path to the repository

    Returns:
    sha - last commit SHA
    """
    repo_path = abspath(repo_path)
    cmd = ['git', '-C', repo_path, 'log', '--pretty=format:%h', '-n', '1']
    _, _sha = run_cmd_with_output(cmd, exit_on_failure=True)

    return _sha


def branch_exists(name):
    """ Check if branch already exists in mbed-os local repository.
    It will not verify if branch is present in remote repository.
    Args:
    name - branch name
    Returns:
    True - If branch is already present
    """

    cmd = ['git', 'branch']
    _, output = run_cmd_with_output(cmd, exit_on_failure=False)
    if name in output:
        return True
    return False


def branch_checkout(name):
    """
    Checkout the required branch
    Args:
    name - branch name
    """
    cmd = ['git', 'checkout', name]
    _, _ = run_cmd_with_output(cmd, exit_on_failure=False)
    rel_log.info("Checkout to branch %s", name)


def get_last_cherry_pick_sha(branch):
    """
    SHA of last cherry pick commit is returned. SHA should be added to all
    cherry-pick commits with -x option.

    Args:
    branch - Hash to be verified.
    Returns - SHA if found, else None
    """
    cmd = ['git', 'checkout', branch]
    run_cmd_with_output(cmd, exit_on_failure=False)

    sha = None
    get_commit = ['git', 'log', '-n', '1']
    _, output = run_cmd_with_output(get_commit, exit_on_failure=True)
    lines = output.split('\n')
    for line in lines:
        if 'cherry picked from' in line:
            sha = line.split(' ')[-1][:-1]
    return sha


def normalize_commit_sha(sha_lst):
    return [_sha['sha'] if isinstance(_sha, dict) else _sha for _sha in sha_lst]


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('-l', '--log-level',
                        help="Level for providing logging output",
                        default='INFO')
    parser.add_argument('-r', '--repo-path',
                        help="Git Repository to be imported",
                        default=None,
                        required=True)
    parser.add_argument('-c', '--config-file',
                        help="Configuration file",
                        default=None,
                        required=True)
    args = parser.parse_args()
    level = getattr(logging, args.log_level.upper())

    if ROOT not in abspath(os.curdir):
        parser.error("This script must be run from the mbed-os directory "
                     "to work correctly.")

    # Set logging level
    logging.basicConfig(level=level)
    rel_log = logging.getLogger("Importer")

    if (args.repo_path is None) or (args.config_file is None):
        rel_log.error(
            "Repository path and config file required as input. "
            "Use \"--help\" for more info.")
        sys.exit(1)

    json_file = abspath(args.config_file)
    if not os.path.isfile(json_file):
        rel_log.error("%s not found.", args.config_file)
        sys.exit(1)

    repo = abspath(args.repo_path)
    if not os.path.exists(repo):
        rel_log.error("%s not found.", args.repo_path)
        sys.exit(1)

    sha = get_curr_sha(repo)
    if not sha:
        rel_log.error("Could not obtain latest SHA")
        sys.exit(1)
    rel_log.info("%s SHA = %s", os.path.basename(repo), sha)

    branch = 'feature_' + os.path.basename(repo) + '_' + sha
    commit_msg = "[" + os.path.basename(repo) + "]" + ": Updated to " + sha

    # Read configuration data
    with open(json_file, 'r') as config:
        json_data = json.load(config)

    '''
    Check if branch exists already, in case branch is present
    we will skip all file transfer and merge operations and will
    jump to cherry-pick
    '''
    if branch_exists(branch):
        rel_log.info("Branch present = %s", branch)
    else:
        data_files = json_data["files"]
        data_folders = json_data["folders"]

    # Remove all files listed in .json from mbed-os repo to avoid duplications
        for fh in data_files:
            src_file = fh['src_file']
            del_file(os.path.basename(src_file))
            dest_file = join(ROOT, fh['dest_file'])
            if isfile(dest_file):
                os.remove(join(ROOT, dest_file))
                rel_log.debug("Deleted %s", fh['dest_file'])

        for folder in data_folders:
            dest_folder = folder['dest_folder']
            delete_dir_files(dest_folder)
            rel_log.debug("Deleted: %s", folder['dest_folder'])

        rel_log.info("Removed files/folders listed in json file")

        # Copy all the files listed in json file to mbed-os
        for fh in data_files:
            repo_file = join(repo, fh['src_file'])
            mbed_path = join(ROOT, fh['dest_file'])
            mkdir(dirname(mbed_path))
            copy_file(repo_file, mbed_path)
            rel_log.debug("Copied %s to %s", normpath(repo_file),
                          normpath(mbed_path))

        for folder in data_folders:
            repo_folder = join(repo, folder['src_folder'])
            mbed_path = join(ROOT, folder['dest_folder'])
            copy_folder(repo_folder, mbed_path)
            rel_log.debug("Copied %s to %s", normpath(repo_folder),
                          normpath(mbed_path))

        # Create new branch with all changes
        create_branch = ['git', 'checkout', '-b', branch]
        run_cmd_with_output(create_branch, exit_on_failure=True)
        rel_log.info("Branch created: %s", branch)

        add_files = ['git', 'add', '-A']
        run_cmd_with_output(add_files, exit_on_failure=True)

        commit_branch = ['git', 'commit', '-m', commit_msg]
        run_cmd_with_output(commit_branch, exit_on_failure=True)
        rel_log.info('Commit added: "%s"', commit_msg)

    # Checkout the feature branch
    branch_checkout(branch)
    commit_sha = normalize_commit_sha(json_data["commit_sha"])
    last_sha = get_last_cherry_pick_sha(branch)

    # Few commits are already applied, check the next in sequence
    # and skip to next commit
    if last_sha:
        assert last_sha in commit_sha, "%s not found in config file" % last_sha
        # Calculate the index of the next sha to be applied
        next_sha_idx = commit_sha.index(last_sha) + 1
        if next_sha_idx >= len(commit_sha):
            rel_log.info("No more commits to apply")
            sys.exit(0)
        # Skipping applied commits
        commit_sha = commit_sha[next_sha_idx:]

    # Apply commits specific to mbed-os changes
    for sha in commit_sha:
        cherry_pick_sha = ['git', 'cherry-pick', '-x', sha]
        rel_log.info("Cherry-picking commit = %s", sha)
        run_cmd_with_output(cherry_pick_sha, exit_on_failure=True)

    rel_log.info("Finished import successfully :)")
