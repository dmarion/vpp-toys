# Build instructions

## Clone and uild vpp release

```bash
git clone https://git.fd.io/vpp
cd vpp
make install-deps
make build-release
cd ..

```

## Clone and build vpp-toys

``` bash
git clone https://github.com/dmarion/vpp-toys.git
cd vpp-toys
make build
```
binaries can be found in `./bin`
