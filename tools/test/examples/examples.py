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
limitations 
"""

from argparse import ArgumentParser
import os
from os.path import dirname, abspath, basename
from prettytable import PrettyTable
import os.path
import sys
import subprocess
import json

""" import and bulid a bunch of example programs """

ROOT = abspath(dirname(dirname(dirname(dirname(__file__)))))
sys.path.insert(0, ROOT)

from tools.utils import argparse_force_uppercase_type
from tools.utils import argparse_many
from tools.build_api import get_mbed_official_release
import examples_lib as lib
from examples_lib import SUPPORTED_TOOLCHAINS, SUPPORTED_IDES

def main():
    """Entry point"""

    official_targets = get_mbed_official_release("5")
    official_target_names = [x[0] for x in official_targets]


    parser = ArgumentParser()
    parser.add_argument("-c", dest="config", default="examples.json")
    parser.add_argument("-e", "--example",
                        help=("filter the examples used in the script"),
                        type=argparse_many(lambda x: x),
                        default=[])
    subparsers = parser.add_subparsers()
    import_cmd = subparsers.add_parser("import", help="import of examples in config file" )
    import_cmd.set_defaults(fn=do_import)
    clone_cmd  = subparsers.add_parser("clone",  help="clone examples in config file" )
    clone_cmd.set_defaults(fn=do_clone)
    list_cmd   = subparsers.add_parser("list",   help="list examples in config file in a table")
    list_cmd.set_defaults(fn=do_list)
    symlink_cmd = subparsers.add_parser("symlink", help="create symbolic link to given mbed-os PATH")
    symlink_cmd.add_argument("PATH", help=" path of mbed-os to be symlinked")
    symlink_cmd.set_defaults(fn=do_symlink)
    deploy_cmd = subparsers.add_parser("deploy", help="mbed deploy for examples in config file" )
    deploy_cmd.set_defaults(fn=do_deploy)
    version_cmd = subparsers.add_parser("update", help="update mbed-os to sepcific tags")
    version_cmd.add_argument("TAG", help=" tag of mbed-os")
    version_cmd.set_defaults(fn=do_update)
    compile_cmd = subparsers.add_parser("compile", help="compile of examples" )
    compile_cmd.set_defaults(fn=do_compile),
    compile_cmd.add_argument(
        "toolchains", nargs="*", default=SUPPORTED_TOOLCHAINS,
        type=argparse_force_uppercase_type(SUPPORTED_TOOLCHAINS,
                                           "toolchain")),
    compile_cmd.add_argument("-m", "--mcu",
                             help=("build for the given MCU (%s)" %
                                ', '.join(official_target_names)),
                            metavar="MCU",
                            type=argparse_many(
                                argparse_force_uppercase_type(
                                    official_target_names, "MCU")),
                            default=official_target_names)

    compile_cmd.add_argument("--profile",
                             help=("build profile file"),
                             metavar="profile")

    compile_cmd.add_argument("-j", "--jobs",
                             dest='jobs',
                             metavar="NUMBER",
                             type=int,
                             default=0,
                             help="Number of concurrent jobs. Default: 0/auto (based on host machine's number of CPUs)")

    compile_cmd.add_argument("-v", "--verbose",
                             action="store_true",
                             dest="verbose",
                             default=False,
                             help="Verbose diagnostic output")

    export_cmd = subparsers.add_parser("export", help="export of examples")
    export_cmd.set_defaults(fn=do_export),
    export_cmd.add_argument(
        "ide", nargs="*", default=SUPPORTED_IDES,
        type=argparse_force_uppercase_type(SUPPORTED_IDES,
                                           "ide"))
    export_cmd.add_argument("-m", "--mcu",
                             help=("build for the given MCU (%s)" %
                                ', '.join(official_target_names)),
                            metavar="MCU",
                            type=argparse_many(
                                argparse_force_uppercase_type(
                                    official_target_names, "MCU")),
                            default=official_target_names)
    args = parser.parse_args()
    config = json.load(open(os.path.join(os.path.dirname(__file__),
                               args.config)))

    all_examples = []
    for example in config['examples']:
        all_examples = all_examples + [basename(x['repo']) for x in lib.get_repo_list(example)]
    examples = [x for x in all_examples if x in args.example] if args.example else all_examples
    return args.fn(args, config, examples)


def do_export(args, config, examples):
    """Do export and build step"""
    results = {}
    results = lib.export_repos(config, args.ide, args.mcu, examples)

    lib.print_summary(results, export=True)
    failures = lib.get_num_failures(results, export=True)
    print("Number of failures = %d" % failures)
    return failures


def do_import(_, config, examples):
    """Do the import step of this process"""
    return lib.source_repos(config, examples)


def do_clone(_, config, examples):
    """Do the clone step of this process"""
    return lib.clone_repos(config, examples)


def do_deploy(_, config, examples):
    """Do the deploy step of this process"""
    return lib.deploy_repos(config, examples)


def do_compile(args, config, examples):
    """Do the compile step"""
    results = lib.compile_repos(config, args.toolchains, args.mcu, args.profile, args.verbose, examples, args.jobs)
    lib.print_summary(results)
    failures = lib.get_num_failures(results)
    print("Number of failures = %d" % failures)
    return failures 
    
def do_update(args, config, examples):
    """ Test update the mbed-os to the version specified by the tag """
    return lib.update_mbedos_version(config, args.tag, examples)

def do_list(_, config, examples):
    """List the examples in the config file"""
    exp_table = PrettyTable()
    exp_table.hrules =  1
    exp_table.field_names = ["Name", "Subs", "Feature", "Targets", "Compile", "Test"]
    for example in config["examples"]:
        exp_table.add_row([example['name'], '\n'.join(example['subs']),'\n'.join(example['features']),'\n'.join(example['targets']),example['compile'],example['test']])
    print(exp_table)
    return 0

def do_symlink(args, config, examples):
    return lib.symlink_mbedos(config, args.path, examples)

if __name__ == "__main__":
    sys.exit(main())
