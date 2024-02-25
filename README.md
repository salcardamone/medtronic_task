Medtronic Task
==============
Dependencies
------------
The following libraries have been used in the development of this project:
- g++ (version 11.4)
- spdlog (version 1.9.2)
- GTest/ GMock (version 1.11.0)
- CMake (version 3.22)

Building Instruction
--------------------
You can build the application and its unit tests as follows:

```bash
$> mkdir build && cd build
$> cmake .. && make
```

This will create a main application, `build/src/medtronic_task`, and a unit test suite
application in `build/test/medtronic_task_tests`.

```bash
$> docker build -t medtronic_task .
$> docker run --rm -it medtronic_task
```

Usage
-----
The main application takes a single argument, the number of concurrent sensors to
instantiate (limited, entirely arbitrarily, to between 1 and 4 sensors):

```bash
$> ./medtronic_task NUM_SENSORS
```

Each sensor will work and log state to the remote host provided in the task specification.
The remote host (en6msadu8lecg.x.pipedream.net) will then be bombarded with sensor states.