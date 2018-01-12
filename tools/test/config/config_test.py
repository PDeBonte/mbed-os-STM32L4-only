"""
mbed SDK
Copyright (c) 2011-2017 ARM Limited

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
import sys
import json
import pytest
from mock import patch
from hypothesis import given
from hypothesis.strategies import sampled_from
from os.path import join, isfile, dirname, abspath
from tools.build_api import get_config
from tools.targets import set_targets_json_location, Target, TARGET_NAMES
from tools.config import ConfigException, Config

def compare_config(cfg, expected):
    """Compare the output of config against a dictionary of known good results

    :param cfg: the configuration to check
    :param expected: what to expect in that config
    """
    try:
        for k in cfg:
            if cfg[k].value != expected[k]:
                return "'%s': expected '%s', got '%s'" % (k, expected[k], cfg[k].value)
    except KeyError:
        return "Unexpected key '%s' in configuration data" % k
    for k in expected:
        if k not in ["expected_macros", "expected_features"] + list(cfg.keys()):
            return "Expected key '%s' was not found in configuration data" % k
    return ""

def data_path(path):
    """The expected data file for a particular test

    :param path: the path to the test
    """
    return join(path, "test_data.json")

def is_test(path):
    """Does a directory represent a test?

    :param path: the path to the test
    """
    return isfile(data_path(path))

root_dir = abspath(dirname(__file__))

@pytest.mark.parametrize("name", filter(lambda d: is_test(join(root_dir, d)),
                                        os.listdir(root_dir)))
def test_config(name):
    """Run a particular configuration test

    :param name: test name (same as directory name)
    """
    test_dir = join(root_dir, name)
    test_data = json.load(open(data_path(test_dir)))
    targets_json = os.path.join(test_dir, "targets.json")
    set_targets_json_location(targets_json if isfile(targets_json) else None)
    for target, expected in test_data.items():
        try:
            cfg, macros, features = get_config(test_dir, target, "GCC_ARM")
            res = compare_config(cfg, expected)
            assert not(res), res
            expected_macros = expected.get("expected_macros", None)
            expected_features = expected.get("expected_features", None)

            if expected_macros is not None:
                macros = Config.config_macros_to_macros(macros)
                assert sorted(expected_macros) == sorted(macros)
            if expected_features is not None:
                assert sorted(expected_features) == sorted(features)
        except ConfigException as e:
            err_msg = str(e)
            if "exception_msg" not in expected:
                assert not(err_msg), "Unexpected Error: %s" % e
            else:
                assert expected["exception_msg"] in err_msg


@pytest.mark.parametrize("target", ["K64F"])
def test_init_app_config(target):
    """
    Test that the initialisation correctly uses app_config

    :param target: The target to use
    """
    set_targets_json_location()
    with patch.object(Config, '_process_config_and_overrides'),\
         patch('tools.config.json_file_to_dict') as mock_json_file_to_dict:
        app_config = "app_config"
        mock_return = {'config': {'test': False}}
        mock_json_file_to_dict.return_value = mock_return

        config = Config(target, app_config=app_config)

        mock_json_file_to_dict.assert_any_call("app_config")

        assert config.app_config_data == mock_return


@pytest.mark.parametrize("target", ["K64F"])
def test_init_no_app_config(target):
    """
    Test that the initialisation works without app config

    :param target: The target to use
    """
    set_targets_json_location()
    with patch.object(Config, '_process_config_and_overrides'),\
         patch('tools.config.json_file_to_dict') as mock_json_file_to_dict:
        config = Config(target)

        mock_json_file_to_dict.assert_not_called()
        assert config.app_config_data == {}


@pytest.mark.parametrize("target", ["K64F"])
def test_init_no_app_config_with_dir(target):
    """
    Test that the initialisation works without app config and with a
    specified top level directory

    :param target: The target to use
    """
    set_targets_json_location()
    with patch.object(Config, '_process_config_and_overrides'),\
         patch('os.path.isfile') as mock_isfile, \
         patch('tools.config.json_file_to_dict') as mock_json_file_to_dict:
        directory = '.'
        path = os.path.join('.', 'mbed_app.json')
        mock_return = {'config': {'test': False}}
        mock_json_file_to_dict.return_value = mock_return
        mock_isfile.return_value = True

        config = Config(target, [directory])

        mock_isfile.assert_called_with(path)
        mock_json_file_to_dict.assert_any_call(path)
        assert config.app_config_data == mock_return


@pytest.mark.parametrize("target", ["K64F"])
def test_init_override_app_config(target):
    """
    Test that the initialisation uses app_config instead of top_level_dir
    when both are specified

    :param target: The target to use
    """
    set_targets_json_location()
    with patch.object(Config, '_process_config_and_overrides'),\
         patch('tools.config.json_file_to_dict') as mock_json_file_to_dict:
        app_config = "app_config"
        directory = '.'
        mock_return = {'config': {'test': False}}
        mock_json_file_to_dict.return_value = mock_return

        config = Config(target, [directory], app_config=app_config)

        mock_json_file_to_dict.assert_any_call(app_config)
        assert config.app_config_data == mock_return
