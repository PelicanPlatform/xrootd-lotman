# Lotman Purge Plugin for XRootD
This purge plugin for [XRootD](https://github.com/xrootd/xrootd) allows caches to purge files according to the policy primitives and usage statistics defined by the [Lotman](https://github.com/pelicanplatform/lotman) library.

## Configuration & Behavior
The plugin assumes that Lotman is set up on the system and that any lots to be tracked by the purge plugin are already instantiated. It also assumes Lotman's "home" is located at `/path/to/lotHome`. For more information about lot configuration and management, see Lotman's documentation using the link above.

The plugin is configured using XRootD's configuration file by specifying [disk usage limits](https://xrootd.slac.stanford.edu/doc/dev56/pss_config.htm#_Toc152615406) (high watermark, low watermark, and file usage), and by specifying the path to the plugin's dynamic library (or the library name if it is installed at a default system location) along with an optional ordered list of space-delimited policies:
```
pfc.diskusage <high watermark> <low watermark> files <baseline> <nominal> <max> purgeinterval <interval>

pfc.purgelib /path/to/libXrdPurgeLotMan.so /path/to/lotHome <ordered policies>
```

Valid policies include:
- `del`: Queues for deletion all files in lots past their "deletion" timestamp. This policy does not delete the lot, but does wipe all storage used by it.
- `exp`: Queues for deletion all files in lots past their "expiration" timestamp. This policy does not delete the lot, but does wipe all storage used by it.
- `opp`: Queues for deletion a portion of the files in lots past their "opportunistic" quota, until the plugin determines the lowest watermark is met or the lot is brought under its opportunistic quota.
- `ded`: Queues for deletion a portion of the files in lots past their "dedicated" quota, until the plugin determines the lowest watermark is met or the lot is brought under its dedicated quota.

**NOTE**: If no policies are configured, the preceding list is used as the default policy list, i.e. it is equivalent to `del exp opp ded`.

**NOTE**: The plugin will only direct the purging of files under its management, and it determines the amount of space to be cleared by the cache independently of how many bytes the cache might think it needs to clear. In the event that the cache thinks it needs to clear more space than is indicated by the plugin, the cache falls back to LRU management until storage usage is brought into compliance with the configured HWM/LWM and file usage directives.

The plugin can use either configured file usage limits, defined as baseline, nominal, and max cumulative file sizes (preferred, see XRootD documentation link above for explanation of disk usage limits), or a configured high/low watermark pair if no file usage limits are provided. When file usage limits are available and when the cache determines the cumulative size of its cached files exceeds the maximum permitted value, the plugin provides the cache with an ordered list of directories to clear that should reduce disk usage to the baseline value. This list is determined by querying Lotman for paths tied to any lot in violation of the current policy being evaluated, where policies are evaluated in the configured order. That is, if the plugin believes it needs to clear 100MB of space and it's configured with `del exp opp ded`, it will start by querying lotman for paths tied to all lots past their deletion time, then paths tied to lots past their expiration time, etc. Because Lotman should be aware of each lot's usage, it will stop implementing these policies once it believes it has provided enough clearable space to the cache.

When no file usage limits are provided, the plugin falls back to using the configured HWM/LWM. However, file usage limits are preferred because they only account for files that are cached directly under the management of XRootD, whereas the HWM/LWM values consider _all_ disk usage including files the cache does not manage.

### Configuration Examples
These examples show only the portions of configuration needed for the plugin, and do not constitute an entire XRootD configuration.

#### Example 1
This configuration will begin clearing cached files when their usage exceeds 50MB, and will continue until usage is brought under 30MB. Purgeable directories are provided by the plugin by first looking for lots past the sum of their opportunistic and dedicated quotas, and then by looking for lots past their dedicated quota. The purge code is triggered once every 10 minutes. Note that the purge library in this case hasn't been installed, so a full path is provided
```
pfc.diskusage 2g 1g files 30m 40m 50m purgeinterval 600s
pfc.purgelib /home/foo/xrootd-lotman/build/libXrdPurgeLotMan.so /lotman opp ded
```

#### Example 2
This configuration begins purging cache files whenever the _disk's_ total usage exceeds 2GB, until the disk is brought under 1GB of usage. Note that the disk's total usage includes files under the cache's management along with any other files stored by the system. The purge code is triggered once every hour, and because no policies are provided in the `pfc.purgelib` directive, the plugin defaults to using `del exp opp ded`.
```
pfc.diskusage 2g 1g purgeinterval 1h
pfc.purgelib libXrdPurgeLotMan.so /lotman
```

## Building and Installation

### Prerequisites
Prerequisites for the plugin include:
- [Lotman](https://github.com/pelicanplatform/lotman)
- [XRootD](https://github.com/xrootd/xrootd) (note that the plugin currently requires a development branch of XRootD located [here](https://github.com/alja/xrootd/tree/purge-main-rb1))

### Installation
To build/install the plugin, from the cloned repository root run:
```bash
mkdir build
cd build
cmake <-DCMAKE_INSTALL_PREFIX=/path/to/desired/location>..
make
make install
```
where the CMake flag is optional.
