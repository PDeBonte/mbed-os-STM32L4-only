from xdg.BaseDirectory import save_data_path
from pycurl import Curl
from bs4 import BeautifulSoup
from os.path import join, dirname, basename
from os import makedirs
from errno import EEXIST
from threading import Thread
from Queue import Queue
from re import compile, sub
from sys import stderr, stdout
from fuzzywuzzy import process
from itertools import takewhile
import argparse
from json import dump, load
from zipfile import ZipFile

RootPackURL = "http://www.keil.com/pack/index.idx"


protocol_matcher = compile("\w*://")
def strip_protocol(url) :
    return protocol_matcher.sub("", str(url))

def largest_version(content) :
    return sorted([t['version'] for t in content.package.releases('release')], reverse=True)[0]

def do_queue(Class, function, interable) :
    q = Queue()
    threads = [Class(q, function) for each in range(20)]
    for each in threads :
        each.setDaemon(True)
        each.start()
    for thing in interable :
        q.put(thing)
    q.join()

class Reader (Thread) :
    def __init__(self, queue, func) :
        Thread.__init__(self)
        self.queue = queue
        self.func = func
    def run(self) :
        while True :
            url = self.queue.get()
            self.func(url)
            self.queue.task_done()

class Cacher (Thread) :
    def __init__(self, queue, func) :
        Thread.__init__(self)
        self.queue = queue
        self.curl = Curl()
        self.curl.setopt(self.curl.FOLLOWLOCATION, True)
        self.func = func
    def run(self) :
        while True :
            url = self.queue.get()
            self.func(self.curl, url)
            self.queue.task_done()


class Cache () :
    """ The Cache object is the only relevant API object at the moment

    Constructing the Cache object does not imply any caching.
    A user of the API must explicitly call caching functions.

    :param silent: A boolean that, when True, significantly reduces the printing of this Object
    :type silent: bool
    :param no_timeouts: A boolean that, when True, disables the default connection timeout and low speed timeout for downloading things.
    :type no_timeouts: bool
    """
    def __init__ (self, silent, no_timeouts) :
        self.silent = silent
        self.counter = 0
        self.total = 1
        self._index = {}
        self.urls = None
        self.no_timeouts = no_timeouts

    def display_counter (self, message) :
        stdout.write("{} {}/{}\r".format(message, self.counter, self.total))
        stdout.flush()

    def cache_file (self, curl, url) :
        """Low level interface to caching a single file.

        :param curl: The user is responsible for providing a curl.Curl object as the curl parameter.
        :type curl: curl.Curl
        :param url: The URL to cache.
        :type url: str
        :rtype: None
        """
        if not self.silent : print("Caching {}...".format(url))
        dest = join(save_data_path('arm-pack-manager'), strip_protocol(url))
        try :
            makedirs(dirname(dest))
        except OSError as exc :
            if exc.errno == EEXIST : pass
            else : raise
        with open(dest, "wb+") as fd :
            curl.setopt(curl.URL, url)
            curl.setopt(curl.FOLLOWLOCATION, True)
            curl.setopt(curl.WRITEDATA, fd)
            if not self.no_timeouts :
                curl.setopt(curl.CONNECTTIMEOUT, 2)
                curl.setopt(curl.LOW_SPEED_LIMIT, 50 * 1024)
                curl.setopt(curl.LOW_SPEED_TIME, 2)
            try :
                curl.perform()
            except Exception as e :
                stderr.write("[ ERROR ] file {} did not download {}\n".format(url, str(e)))
        self.counter += 1
        self.display_counter("Caching Files")

    def pdsc_to_pack (self, url) :
        """Find the URL of the specified pack file described by a PDSC.

        The PDSC is assumed to be cached and is looked up in the cache by its URL.

        :param url: The url used to look up the PDSC.
        :type url: str
        :return: The url of the PACK file.
        :rtype: str
        """
        content = self.pull_from_cache(url)
        new_url = content.package.url.get_text()
        if not new_url.endswith("/") :
            new_url = new_url + "/"
        return (new_url + content.package.vendor.get_text() + "." +
                content.package.find('name').get_text() + "." +
                largest_version(content) + ".pack")

    def cache_pdsc_and_pack (self, curl, url) :
        self.cache_file(curl, url)
        try :
            self. cache_file(curl, self.pdsc_to_pack(url))
        except AttributeError :
            stderr.write("[ ERROR ] {} does not appear to be a conforming .pdsc file\n".format(url))
            self.counter += 1

    def get_urls(self):
        """Extract the URLs of all know PDSC files.

        Will pull the index from the internet if it is not cached.

        :return: A list of all PDSC URLs
        :rtype: [str]
        """
        if not self.urls :
            try : root_data = self.pull_from_cache(RootPackURL)
            except IOError : root_data = self.cache_and_parse(RootPackURL)
            self.urls = [join(pdsc.get('url'), pdsc.get('name')) for pdsc in root_data.find_all("pdsc")]
        return self.urls

    def _extract_dict(self, device, filename) :
        to_ret = dict(file=filename)
        try :
            to_ret["memory"] = dict([(m["id"], dict(start=m["start"],
                                                    size=m["size"]))
                                     for m in device("memory")])
            to_ret["algorithm"] = device.algorithm["name"].replace('\\','/')
            to_ret["debug"] = device.debug["svd"]
            to_ret["compile"] = (device.compile["header"], device.compile["define"])
        except (KeyError, TypeError) :
            pass
        return to_ret

    def _generate_index_helper(self, d) :
        try :
            self._index.update(dict([(dev['dname'], self._extract_dict(dev, d)) for dev in
                                    (self.pull_from_cache(d)("device"))]))
        except AttributeError as e :
            print(e)
        self.counter += 1
        self.display_counter("Generating Index")

    def get_flash_algorthim_binary(self, device_name) :
        """Retrieve the flash algorithm file for a particular part.

        Assumes that both the PDSC and the PACK file associated with that part are in the cache.

        :param device_name: The exact name of a device
        :type device_name: str
        :return: A file-like object that, when read, is the ELF file that describes the flashing algorithm
        :rtype: ZipExtFile
        """
        device = self.index[device_name]
        pack = ZipFile(join(save_data_path('arm-pack-manager'),
                            strip_protocol(self.pdsc_to_pack(device['file']))))
        return pack.open(device['algorithm'])

    def generate_index(self) :
        self._index = {}
        self.counter = 0
        do_queue(Reader, self._generate_index_helper, self.get_urls())
        with open(join(save_data_path('arm-pack-manager'), "index.json"), "wb+") as out:
            dump(self._index, out)
        stdout.write("\n")

    def find_device(self, match) :
        choices = process.extract(match, self.index.keys(), limit=len(self.index))
        choices = sorted([(v, k) for k, v in choices], reverse=True)
        if not choices : return []
        else : choices = list(takewhile(lambda t: t[0] == choices[0][0], choices))
        return [(v, self.index[v]) for k,v in choices]

    def dump_index_to_file(self, file) :
        with open(file, "wb+") as out:
            dump(self.index, out)

    @property
    def index(self) :
        """An index of most of the important data in all cached PDSC files.

        :Example:

        >>> from ArmPackManager import Cache
        >>> a = Cache()
        >>> a.index["LPC1768"]
        {u'algorithm': u'Flash/LPC_IAP_512.FLM',
         u'compile': [u'Device/Include/LPC17xx.h', u'LPC175x_6x'],
         u'debug': u'SVD/LPC176x5x.svd',
         u'file': u'http://www.keil.com/pack/Keil.LPC1700_DFP.pdsc',
         u'memory': {u'IRAM1': {u'size': u'0x8000', u'start': u'0x10000000'},
                     u'IRAM2': {u'size': u'0x8000', u'start': u'0x2007C000'},
                     u'IROM1': {u'size': u'0x80000', u'start': u'0x00000000'}}}

        """
        if not self._index :
            try :
                with open(join(save_data_path('arm-pack-manager'), "index.json")) as i :
                    self._index = load(i)
            except IOError :
                self.generate_index()
        return self._index

    def cache_everything(self) :
        """Cache every PACK and PDSC file known.

        Generates an index afterwards.

        .. note:: This process may use 4GB of drive space and take upwards of 10 minutes to complete.
        """
        self.cache_pack_list(self.get_urls())
        self.generate_index()

    def cache_descriptors(self) :
        """Cache every PDSC file known.

        Generates an index afterwards.

        .. note:: This process may use 11MB of drive space and take upwards of 1 minute.
        """
        self.cache_descriptor_list(self.get_urls())
        self.generate_index()

    def cache_descriptor_list(self, list) :
        """Cache a list of PDSC files.

        :param list: URLs of PDSC files to cache.
        :type list: [str]
        """
        self.total = len(list)
        self.display_counter("Caching Files")
        do_queue(Cacher, self.cache_file, list)
        stdout.write("\n")

    def cache_pack_list(self, list) :
        """Cache a list of PACK files, referenced by their PDSC URL

        :param list: URLs of PDSC files to cache.
        :type list: [str]
        """
        self.total = len(list) * 2
        self.display_counter("Caching Files")
        do_queue(Cacher, self.cache_pdsc_and_pack, list)
        stdout.write("\n")

    def pull_from_cache(self, url) :
        """Low level inteface for extracting a PDSC file from the cache.

        Assumes that the file specified is a PDSC file and is in the cache.

        :param url: The URL of a PDSC file.
        :type url: str
        :return: A parsed representation of the PDSC file.
        :rtype: BeautifulSoup
        """
        dest = join(save_data_path('arm-pack-manager'), strip_protocol(url))
        with open(dest, "r") as fd :
            return BeautifulSoup(fd, "html.parser")

    def gen_dict_from_cache() :
        pdsc_files = pull_from_cache(RootPackUrl)

    def cache_and_parse(self, url) :
        """A low level shortcut that Caches and Parses a PDSC file.

        :param url: The URL of the PDSC file.
        :type url: str
        :return: A parsed representation of the PDSC file.
        :rtype: BeautifulSoup
        """
        self.cache_file(Curl(), url)
        return self.pull_from_cache(url)

