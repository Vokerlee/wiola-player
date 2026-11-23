# Native Core SDK tests

Controlled compiler, pkg-config, CMake, symbol and loader processes exercise the actual recipe without compiling Qt. The source port is supplied by the separately tested common Qt source provider. Cover context changes, reuse and corruption, configured artifact lookup, missing instrumentation, failed configure/build, runtime publication and consumer resolution. A real Core build and affected TSan consumers provide integration evidence separately.
