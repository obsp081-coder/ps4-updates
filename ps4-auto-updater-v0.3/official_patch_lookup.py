
"""Discover official PS4 retail patch metadata.

Read-only metadata requests.
Never downloads PKGs or confirms FPKG compatibility.

Endpoint method documented by:
andshrew/PS4-Updates-Python (MIT).
"""

import argparse
import hashlib
import hmac
import json
import re
from urllib.error import HTTPError, URLError
from urllib.request import Request, HTTPRedirectHandler, build_opener
from xml.etree import ElementTree as ET

CUSA_RE = re.compile(r"CUSA[0-9]{5}\Z")
VERSION_RE = re.compile(r"[0-9]{1,4}\.[0-9]{1,4}\Z")

CONTENT_RE = re.compile(
    r"[A-Z]{2}[0-9]{4}-"
    r"(CUSA[0-9]{5})_[0-9]{2}-"
    r"[A-Z0-9]{16}\Z"
)

# Public Sony update URL calculation value.
# Not a user password or private credential.
PATCH_URL_KEY = bytes.fromhex(
    "AD62E37F905E06BC19593142281C112C"
    "EC0E7EC3E97EFDCAEFCDBAAFA6378D84"
)

ENDPOINT = (
    "http://gs-sec.ww.np.dl.playstation.net/plo/np"
)

MAX_XML = 256 * 1024


def patch_url(cusa):
    if (
        not isinstance(cusa, str)
        or not CUSA_RE.fullmatch(cusa)
    ):
        raise ValueError(
            "CUSA must look like CUSA02343"
        )

    digest = hmac.new(
        PATCH_URL_KEY,
        f"np_{cusa}".encode("ascii"),
        hashlib.sha256
    ).hexdigest()

    return (
        f"{ENDPOINT}/{cusa}/{digest}/"
        f"{cusa}-ver.xml"
    )


class _RejectRedirect(HTTPRedirectHandler):
    """Never follow an untrusted redirect from the HTTP metadata endpoint."""

    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


def read_xml(url):
    """Request only the exact generated Sony URL; never follow redirects.

    Sony's historical endpoint uses plaintext HTTP. Data from it is
    **unauthenticated metadata**, never installation/download approval.
    """
    if not isinstance(url, str):
        raise ValueError("Sony metadata URL must be a string")
    match = re.fullmatch(
        re.escape(ENDPOINT) + r"/(CUSA[0-9]{5})/[a-f0-9]{64}/"
        + r"(CUSA[0-9]{5})-ver\.xml",
        url,
    )
    if not match or match.group(1) != match.group(2) or url != patch_url(match.group(1)):
        raise ValueError("Refusing non-Sony or altered patch metadata URL")

    request = Request(url, headers={
        "User-Agent": "PS4-Auto-Updater-Metadata/0.4"
    })
    with build_opener(_RejectRedirect()).open(request, timeout=12) as response:
        data = response.read(MAX_XML + 1)

    if len(data) > MAX_XML:
        raise ValueError(
            "Metadata response too large"
        )

    return data


def parse_patch_xml(data, cusa):
    if (
        not isinstance(data, bytes)
        or len(data) > MAX_XML
    ):
        raise ValueError(
            "Invalid XML data length"
        )

    try:
        root = ET.fromstring(data)
    except ET.ParseError as error:
        raise ValueError(
            "Invalid patch XML"
        ) from error

    if root.tag != "titlepatch":
        raise ValueError(
            "Unexpected XML root"
        )

    candidates = []

    for pkg in root.iter("package"):
        version = pkg.get("version")
        content_id = pkg.get("content_id")

        match = CONTENT_RE.fullmatch(
            content_id or ""
        )

        if not (
            VERSION_RE.fullmatch(version or "")
            and match
            and match.group(1) == cusa
        ):
            continue

        name = (
            pkg.findtext("./paramsfo/title")
            or ""
        )

        candidates.append((
            tuple(
                int(value)
                for value in version.split(".")
            ),
            version,
            content_id,
            name.strip()[:256]
        ))

    if not candidates:
        raise ValueError(
            "No matching version and content_id"
        )

    _, version, content_id, name = max(candidates)

    return {
        "latest_retail_version": version,
        "content_id": content_id,
        "title": name
    }


def inspect_title(cusa, fetcher=read_xml):
    url = patch_url(cusa)

    result = {
        "cusa": cusa,
        "status": "LOOKUP_FAILED",
        "source": "sony-retail-patch-xml",
        "metadata_url": url,
        "transport_authenticated": False,
        "rights_verified": False,
        "fpkg_compatibility_confirmed": False,
        "install_enabled": False,
        "downloads_enabled": False,
    }

    try:
        data = fetcher(url)
        result.update(
            parse_patch_xml(data, cusa)
        )
        result["status"] = "RETAIL_PATCH_LISTED"

    except HTTPError as error:
        result["status"] = (
            "NOT_LISTED"
            if error.code == 404
            else "LOOKUP_FAILED"
        )
        result["http_status"] = error.code

    except (
        URLError,
        TimeoutError,
        OSError,
        ValueError
    ) as error:
        result["error_type"] = (
            type(error).__name__
        )

    return result


def discover(cusas, fetcher=read_xml):
    unique = list(dict.fromkeys(cusas))

    if not unique or len(unique) > 100:
        raise ValueError(
            "Provide 1 to 100 CUSA identifiers"
        )

    # Validate everything before network requests.
    for cusa in unique:
        patch_url(cusa)

    return {
        "schema": 1,
        "source_kind": "official_retail_metadata",
        "fpkg_compatibility_confirmed": False,
        "install_enabled": False,
        "downloads_enabled": False,
        "titles": [
            inspect_title(cusa, fetcher)
            for cusa in unique
        ]
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__
    )

    parser.add_argument(
        "--cusa",
        nargs="+",
        required=True
    )

    parser.add_argument(
        "--out",
        default="retail_patch_metadata.json"
    )

    args = parser.parse_args()

    try:
        report = discover(args.cusa)
    except ValueError as error:
        parser.error(str(error))

    with open(
        args.out, "w", encoding="utf-8"
    ) as file:
        json.dump(
            report,
            file,
            indent=2,
            ensure_ascii=False
        )
        file.write("\n")

    for row in report["titles"]:
        print(
            f'{row["cusa"]}: '
            f'{row["status"]} '
            f'{row.get("latest_retail_version", "")}'
        )

    print(
        "Retail metadata only; "
        "FPKG compatibility NOT verified"
    )


if __name__ == "__main__":
    main()
