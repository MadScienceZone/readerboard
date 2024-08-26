# Readerboard Server
This provides a network service that gives you a point of contact that various clients, including busylight status trackers, can use to set messages and status on a collection of connected readerboards and busylights.
The actual devices can be on one or more direct USB connections to the host running the server, and/or one or more RS-485 networks.

## Protocol
The messaging protocol is simple. Every message is a newline-terminated plain-text Unicode string encoded as a JSON object. Clients SHOULD accept any valid UTF-8 character encoding, but SHOULD use JSON standard escape codes to keep the actual text conformant to 7-bit ASCII.
Newlines MUST NOT appear in the middle of a message.
In every message, any expected fields which are missing are assumed without error to have the appropriate "zero value" for their type. Any fields presented which were not expected are discarded silently.
After establishing a TCP connection to the server, the server sends a greeting with at least these fields:
    m: hello
    v: API/protocol version number; should be 1 for this version of the spec
    auth: authentication challenge if required by the server configuration
    access: "granted", "denied", or "auth" if authentication required

The client can log in with a message
    m: auth
    auth: credentials

Response:
    m: access
    access: "granted" or "denied"

Similar messages are accepted from the client to post messages:
    m: operation type
    a: sign address(es) to target
    other values as appropriate for the operation type

Detach (either client or server):
    m: bye
    reason: text
