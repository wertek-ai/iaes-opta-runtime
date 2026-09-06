# Security

## What this runtime does not protect

It publishes **plaintext MQTT**. There is no TLS, and there is no setting that
claims otherwise.

That is stated first because the alternative was worse: until 2026-09-06 the
configuration carried a `use_tls` flag that nothing read, so an integrator who
set it to `true` believed the link was encrypted while the broker password went
out in the clear. The flag is gone rather than left to mislead.

Securing the link is the deployment's:

- a broker that terminates TLS, with this runtime behind it on a trusted segment
- a VPN or a dedicated OT network
- network segmentation between the Modbus segment and anything routable

IAES defines no transport, so transport security is outside the standard as
well as outside this runtime. That is a boundary, not an oversight.

## What it does protect

Nothing is claimed. This is a Modbus reader and an event builder. It stores no
credentials beyond what the sketch passes it, holds no keys, and terminates no
sessions.

## The broker password

`MqttConfig::password` lives in RAM and is sent in the MQTT CONNECT packet.
Treat it as recoverable by anyone with the device, the network, or the sketch.

## Reporting

Open an issue for anything that is safe to discuss publicly. For anything that
is not, write to engineering@wertek.ai rather than opening an issue.

Please include the commit, what you did, and what happened. There is no bounty.

## Scope

This repository. The IAES specification itself lives in
[wertek-ai/iaes](https://github.com/wertek-ai/iaes); a defect in the standard
belongs there.
