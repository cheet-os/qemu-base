## Install qemu to alpine
docker run --rm \
  -v /home/texhik/sources/vm-qemu:/src:ro \
  -v /home/texhik/sources/vm-qemu/build-alpine:/build \
  -v $(pwd)/rootfs/overlay:/out \
  alpine:3.21 sh -c '
    apk add build-base python3 meson ninja glib-dev pixman-dev \
            linux-headers gnutls-dev libslirp-dev git bash \
            dtc-dev zstd-dev libcap-ng-dev libaio-dev \
            libusb-dev usbredir-dev
    cp -a /src /tmp/qemu-src
    rm -rf /build/*
    cd /build
    /tmp/qemu-src/configure --prefix=/usr --target-list=x86_64-softmmu \
        --enable-kvm --enable-libusb --enable-usb-redir --disable-werror \
        --extra-cflags="-w"
    make -j$(nproc)
    make DESTDIR=/out install
'
