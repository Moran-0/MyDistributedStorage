# RPC Demo

This directory contains a minimal echo service used to validate the RPC framework.

## Files

- `rpc_demo.proto`: an echo service definition
- `rpc_demo_config.json`: runtime config for server and ZooKeeper

## Build

Generate protobuf files first:

```bash
protoc --cpp_out=example/rpc_demo --proto_path=example/rpc_demo example/rpc_demo/rpc_demo.proto
```

Then build the demo targets:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

1. Start ZooKeeper on `127.0.0.1:2181`.
2. Start the server:
   `./bin/echo_server example/rpc_demo/rpc_demo_config.json`
3. Start the client:
   `./bin/echo_client example/rpc_demo/rpc_demo_config.json hello`

Expected output:

`echo: hello`
