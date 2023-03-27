CAIRO=enabled        # disabled|enabled
PNG_BACKEND=libpng   # none|libpng
SVG_BACKEND=librsvg  # none|librsvg|nanosvg (librsvg force-enables cairo, nanosvg is bundled)

pkgname=fuzzel
pkgver=1.9.0
pkgrel=1
pkgdesc="Simplistic application launcher for wayland"
arch=('x86_64' 'aarch64')
url=https://codeberg.org/dnkl/fuzzel
license=(mit)
makedepends=('meson' 'ninja' 'scdoc' 'wayland-protocols' 'tllist>=1.0.1')
depends=('libxkbcommon' 'wayland' 'pixman' 'fcft>=3.0.0' 'fcft<4.0.0')
source=()

if [[ ${PNG_BACKEND} == libpng ]]; then
    depends+=( 'libpng' )
fi

if [[ ${SVG_BACKEND} == librsvg ]]; then
    depends+=( 'librsvg' )
    CAIRO=enabled
fi

if [[ ${CAIRO} == enabled ]]; then
    depends+=( 'cairo' )
fi

pkgver() {
  cd ../.git &> /dev/null && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
      head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  meson \
      --prefix=/usr                \
      --buildtype=release          \
      --wrap-mode=nofallback       \
      -Denable-cairo=${CAIRO}      \
      -Dpng-backend=${PNG_BACKEND} \
      -Dsvg-backend=${SVG_BACKEND} \
      ..
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
