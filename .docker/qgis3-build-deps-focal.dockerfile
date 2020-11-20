FROM      ubuntu:20.04
MAINTAINER Denis Rouzaud <denis@opengis.ch>

LABEL Description="Docker container with QGIS dependencies" Vendor="QGIS.org" Version="1.0"

# && echo "deb http://ppa.launchpad.net/ubuntugis/ubuntugis-unstable/ubuntu xenial main" >> /etc/apt/sources.list \
# && echo "deb-src http://ppa.launchpad.net/ubuntugis/ubuntugis-unstable/ubuntu xenial main" >> /etc/apt/sources.list \
# && apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 314DF160 \


RUN  apt-get update \
  && apt-get install -y software-properties-common \
  && apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    apt-transport-https \
    bison \
    ca-certificates \
    ccache \
    clang \
    cmake \
    curl \
    dh-python \
    flex \
    gdal-bin \
    git \
    graphviz \
    grass-dev \
    libaio1 \
    libexiv2-dev \
    libexpat1-dev \
    libfcgi-dev \
    libgdal-dev \
    libgeos-dev \
    libgsl-dev \
    libpq-dev \
    libproj-dev \
    libqca-qt5-2-dev \
    libqca-qt5-2-plugins \
    libqt53dextras5 \
    libqt53drender5 \
    libqt5concurrent5 \
    libqt5opengl5-dev \
    libqt5positioning5 \
    libqt5qml5 \
    libqt5quick5 \
    libqt5quickcontrols2-5 \
    libqt5scintilla2-dev \
    libqt5sql5-odbc \
    libqt5sql5-sqlite \
    libqt5svg5-dev \
    libqt5webkit5-dev \
    libqt5xml5 \
    libqt5serialport5-dev \
    libqwt-qt5-dev \
    libspatialindex-dev \
    libspatialite-dev \
    libsqlite3-dev \
    libsqlite3-mod-spatialite \
    libzip-dev \
    libzstd-dev \
    lighttpd \
    locales \
    ninja-build \
    pkg-config \
    poppler-utils \
    postgresql-client \
    protobuf-compiler \
    pyqt5-dev \
    pyqt5-dev-tools \
    pyqt5.qsci-dev \
    python3-all-dev \
    python3-dev \
    python3-future \
    python3-gdal \
    python3-mock \
    python3-nose2 \
    python3-owslib \
    python3-pip \
    python3-psycopg2 \
    python3-pyqt5 \
    python3-pyqt5.qsci \
    python3-pyqt5.qtsql \
    python3-pyqt5.qtsvg \
    python3-sip \
    python3-sip-dev \
    python3-termcolor \
    python3-yaml \
    qt3d5-dev \
    qt3d-assimpsceneimport-plugin \
    qt3d-defaultgeometryloader-plugin \
    qt3d-gltfsceneio-plugin \
    qt3d-scene2d-plugin \
    qt5keychain-dev \
    qtbase5-dev \
    qtdeclarative5-dev-tools \
    qtpositioning5-dev \
    qttools5-dev \
    qttools5-dev-tools \
    qtbase5-private-dev \
    saga \
    spawn-fcgi \
    supervisor \
    unzip \
    xauth \
    xfonts-100dpi \
    xfonts-75dpi \
    xfonts-base \
    xfonts-scalable \
    xvfb \
  && pip3 install \
    psycopg2 \
    numpy \
    nose2 \
    pyyaml \
    mock \
    future \
    termcolor \
    oauthlib \
    pyopenssl \
  && apt-get clean

# Oracle : client side
RUN curl https://download.oracle.com/otn_software/linux/instantclient/193000/instantclient-basic-linux.x64-19.3.0.0.0dbru.zip > instantclient-basic-linux.x64-19.3.0.0.0dbru.zip
RUN curl https://download.oracle.com/otn_software/linux/instantclient/193000/instantclient-sdk-linux.x64-19.3.0.0.0dbru.zip > instantclient-sdk-linux.x64-19.3.0.0.0dbru.zip
RUN curl https://download.oracle.com/otn_software/linux/instantclient/193000/instantclient-sqlplus-linux.x64-19.3.0.0.0dbru.zip > instantclient-sqlplus-linux.x64-19.3.0.0.0dbru.zip

RUN unzip instantclient-basic-linux.x64-19.3.0.0.0dbru.zip
RUN unzip instantclient-sdk-linux.x64-19.3.0.0.0dbru.zip
RUN unzip instantclient-sqlplus-linux.x64-19.3.0.0.0dbru.zip

ENV PATH="/instantclient_19_3:${PATH}"
ENV LD_LIBRARY_PATH="/instantclient_19_3:${LD_LIBRARY_PATH}"

# MSSQL: client side
RUN curl https://packages.microsoft.com/keys/microsoft.asc | apt-key add -
RUN curl https://packages.microsoft.com/config/ubuntu/19.04/prod.list | tee /etc/apt/sources.list.d/msprod.list
RUN apt-get update
RUN ACCEPT_EULA=Y apt-get install -y --allow-unauthenticated msodbcsql17 mssql-tools

# Avoid sqlcmd termination due to locale -- see https://github.com/Microsoft/mssql-docker/issues/163
RUN echo "nb_NO.UTF-8 UTF-8" > /etc/locale.gen
RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen
RUN locale-gen

RUN echo "alias python=python3" >> ~/.bash_aliases

ENV QT_SELECT=5
ENV LANG=C.UTF-8
ENV PATH="/usr/local/bin:${PATH}"
