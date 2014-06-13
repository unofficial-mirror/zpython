# Copyright 1999-2014 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=5

PYTHON_COMPAT=( python{2_7,3_2,3_3} )

inherit mercurial python-single-r1 cmake-utils

DESCRIPTION="Zsh python bindings module"
HOMEPAGE="https://bitbucket.org/ZyX_I/zpython"

LICENSE=""
SLOT="0"
KEYWORDS="~amd64"
IUSE="doc"

DEPEND="${DEPEND}
	${PYTHON_DEPS}
	doc? ( app-text/yodl )
"
RDEPEND="${DEPEND}
	app-shells/zsh
"

EHG_REPO_URI="https://bitbucket.org/ZyX_I/zpython"

src_configure() {
	mycmakeargs="-DPython_ADDITIONAL_VERSIONS=${PYTHON_SINGLE_TARGET//_/.}"
	mycmakeargs="${MYCMAKEARGS/python}"
	cmake-utils_src_configure
}

src_compile() {
	if use doc ; then
		cmake-utils_src_compile doc
	fi
	cmake-utils_src_compile
}
