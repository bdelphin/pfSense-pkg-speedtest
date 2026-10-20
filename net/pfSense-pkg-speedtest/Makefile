PORTNAME=	pfSense-pkg-speedtest
PORTVERSION=	0.1.0
CATEGORIES=	net
MASTER_SITES=	# empty
DISTFILES=	# empty
EXTRACT_ONLY=	# empty

MAINTAINER=	baptiste.delphin@gmail.com
COMMENT=	pfSense package to measure bandwidth against speedtest.net servers

LICENSE=	APACHE20

NO_BUILD=	yes
NO_MTREE=	yes

SUB_FILES=	pkg-install pkg-deinstall
SUB_LIST=	PORTNAME=${PORTNAME}

do-extract:
	${MKDIR} ${WRKSRC}

do-install:
	${MKDIR} ${STAGEDIR}${PREFIX}/pkg/speedtest
	${MKDIR} ${STAGEDIR}${PREFIX}/www
	${MKDIR} ${STAGEDIR}${PREFIX}/bin
	${MKDIR} ${STAGEDIR}/etc/inc/priv
	${MKDIR} ${STAGEDIR}${DATADIR}
	${INSTALL_DATA} -m 0644 ${FILESDIR}${PREFIX}/pkg/speedtest.xml \
		${STAGEDIR}${PREFIX}/pkg
	${INSTALL_DATA} ${FILESDIR}${PREFIX}/pkg/speedtest.inc \
		${STAGEDIR}${PREFIX}/pkg
	${INSTALL_DATA} ${FILESDIR}${PREFIX}/pkg/speedtest/engine.inc \
		${FILESDIR}${PREFIX}/pkg/speedtest/store.inc \
		${FILESDIR}${PREFIX}/pkg/speedtest/run.php \
		${STAGEDIR}${PREFIX}/pkg/speedtest
	${INSTALL_DATA} ${FILESDIR}${PREFIX}/www/diag_speedtest.php \
		${STAGEDIR}${PREFIX}/www
	${INSTALL_SCRIPT} ${FILESDIR}${PREFIX}/bin/pfspeedtest \
		${STAGEDIR}${PREFIX}/bin
	${INSTALL_DATA} ${FILESDIR}/etc/inc/priv/speedtest.priv.inc \
		${STAGEDIR}/etc/inc/priv
	${INSTALL_DATA} ${FILESDIR}${DATADIR}/info.xml \
		${STAGEDIR}${DATADIR}
	@${REINPLACE_CMD} -i '' -e "s|%%PKGVERSION%%|${PKGVERSION}|" \
		${STAGEDIR}${DATADIR}/info.xml

.include <bsd.port.mk>
