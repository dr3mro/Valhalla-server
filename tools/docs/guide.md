REST API of the **Valhalla** healthcare backend (Drogon / C++20, PostgreSQL). Every URL below is relative to the base URL
`http://<host>:<port>/api/v2` (default `http://127.0.0.1:8080/api/v2`).


## Quick start

```bash
BASE=http://127.0.0.1:8080/api/v2
curl $BASE/hello                                   # 1. is the server up?
curl -X POST $BASE/clients/providers/create \
  -H 'Content-Type: application/json' \
  -d '{"username":"dr_amr","password":"1234@Abc!","fullname":"Dr Amr","email":"a@b.com","phone":"+201299999999","language":"en","active":true,
       "country":"Egypt","city":"Cairo","address":"Street 1","dob":"17-02-1985","gender":"Male","position":"Doctor","speciality":"Cardiology"}'   # 2. register -> {"id": 1}
curl -X POST $BASE/clients/providers/login \
  -H 'Content-Type: application/json' -d '{"username":"dr_amr","password":"1234@Abc!"}'                                                      # 3. -> {"token": "...", "client_id": 1}
curl -X POST $BASE/services/clinics/read -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' -d '{"id": 1, "schema": []}'                                                                           # 4. use the token
```

## How the API works

| Topic | Rule |
|---|---|
| **Format** | JSON in, JSON out. Send `Content-Type: application/json` on requests with a body. |
| **Authentication** | `POST /clients/providers/login` or `/clients/users/login` returns a JWT. Send it as `Authorization: Bearer <token>` on every other call except `hello`, `create` and `login`. |
| **Ids** | Created records return `{"id": <number>}`. `read` takes the id in the **body** (`{"id": 1, "schema": []}`); `update`, `delete`, `activate`, `suspend` and `services` take it in the **query string** (`?id=1`). |
| **Searching** | `search` bodies have `keyword`, `filter` (column), `limit`, `offset`, `order_by`, `direction`. |
| **Partial reads** | `schema` in a read body lists the columns to return; an empty array returns all columns. |
| **Updates** | `PUT` replaces the editable columns you send. Keys are checked against the table columns; unknown keys are a 400. |
| **Delete** | Only accounts, services, appointments, **patients** and **visits** can be deleted. Health, drug, report, request, prescription and paid-service rows answer `400 Delete is NOT implemented` and go away with their parent through `ON DELETE CASCADE`. |
| **Suspension** | A suspended account is locked out of every endpoint, including `activate` and `delete`. |
| **Unknown routes** | JSON `404` `"Endpoint not found."` |
| **CORS** | `OPTIONS` answers `204` with `Access-Control-Allow-Origin: *`, methods `GET, POST, PUT, DELETE, OPTIONS`, headers `Content-Type, Authorization`. |
| **Dates and times** | Date of birth of accounts is `dd-mm-yyyy` (validated). Patient `dob` and every other date use `yyyy-mm-dd`; times are `hh:mm` or `hh:mm:ss`. |
| **JSON columns** | `staff`, `phone` (of services), `specialities`, `health`, `drugs`, `reports`, `requests`, `prescriptions`, `paidservices`, `examination` are free-form JSON stored as `JSONB`. |

## Authentication in detail

1. **Register** with `POST /clients/providers/create` or `/clients/users/create`. A *provider* is a healthcare professional and can own services; a *user* is a patient-side account and is needed for appointments.
2. **Log in** with `POST /clients/{group}/login`. The response is `{"token": "<JWT>", "client_id": <id>}`.
3. **Send the token** on every request: `Authorization: Bearer <token>`. The header must start with `Bearer ` and a space.
4. **Token rules**
   - It is valid for `TOKEN_VALIDITY` minutes (default **43200** = 30 days).
   - It is bound to the **IP address** that logged in; use it from another IP and you get `401 Token ip address does not match user ip address`.
   - `POST /clients/{group}/logout` revokes it immediately (`401 Client is logged out from server, Please login again.`). Log in again for a new token.
   - Suspending or deleting the account also ends its session.
5. A token carries the caller's **group** (`providers` or `users`) and **id**; both are checked against the record being touched.

## Permissions

Access is checked in two layers after the token is accepted.

### 1. Accounts (providers and users): self only

`read`, `update`, `delete`, `activate`, `suspend` and `services` on a client are allowed **only for the account itself**: the token's group must match the route (`providers` or `users`) and the token's id must equal the `id` you pass. Anything else is `403 You are not allowed to manage client with id: <id> and group: <group>`.
`create` and `login` are public. `search` only needs a valid token.

### 2. Services and clinical data: owner, admin or staff

Every service row (clinic, pharmacy, laboratory, radiology center) stores `owner_id`, `admin_id` and a `staff` object:

```json
{"owner_id": 1, "admin_id": 1,
 "staff": {"Doctors": ["1:28", "7:12"], "Nurses": ["9:4"]}}
```

Each staff entry is `"<provider id>:<power>"`, where `power` is a bit mask (below). Roles (`Doctors`, `Nurses`, ...) are free labels used to group people; the permission comes only from the number after the colon.

| Bit | Value | Name | Meaning |
|---|---|---|---|
| 0 | `0x01` | `USER` | account group flag (user) |
| 1 | `0x02` | `PROVIDER` | account group flag (provider) |
| 2 | `0x04` | `CAN_READ` | read |
| 3 | `0x08` | `CAN_WRITE` | write |
| 4 | `0x10` | `CAN_DELETE` | delete |
| 5 | `0x20` | `IS_ADMIN` | administrator |
| 6 | `0x40` | `IS_OWNER` | owner |
| 7 | `0x80` | `IS_SUPER` | super user |

Combine bits by adding: read + write + delete = 4 + 8 + 16 = **28**; read + write = **12**.

**Who may do what** (owner and admin always pass; other staff need the bit shown)

| Operation | Staff bit needed |
|---|---|
| Create a service | not staff-based: a **provider** whose own id is both `owner_id` and `admin_id` in the body (otherwise `400`) |
| Read a service | `CAN_READ` (`0x04`) |
| Update a service | `CAN_WRITE` (`0x08`) |
| **Delete** a service | **owner only** |
| Manage staff (`invite`, `add`, `remove`) | owner or admin only |
| Patients, health, drugs, reports, visits, requests, prescriptions, paid services: **read / search-by-id** | `CAN_READ` (`0x04`) of the patient's clinic |
| ... **create / update** | `CAN_WRITE` (`0x08`) |
| ... **delete** (patients and visits) | `CAN_DELETE` (`0x10`) |
| Appointments: read | `CAN_READ` (`0x04`) |
| Appointments: create / update | `CAN_WRITE` (`0x08`) |
| Appointments: delete | `CAN_DELETE` (`0x10`) |

The bits are independent, so `4` is a read-only member, `12` can read and write, and `28` can also delete. A bit is tested with a bitwise AND: a staff entry passes when it contains the required bit.

**Permission errors**

| Message | Status | Meaning |
|---|---|---|
| `You are not allowed to manage client with id: <id> and group: <group>` | 403 | Account endpoint called with another account's id or the wrong group. |
| `You are not a staff of this service` | 403 | Caller is not listed in the service's `staff`. |
| `Failed to find the user permission level in service <name>` | 403 | Staff lookup found no matching entry. |
| `You don't have the permission to manage <service> ...` | 403 | Staff entry exists but lacks the required bit. |
| `You are not the owner of <service>` / `You are not the admin of <service>` | 403 | Appended while checking owner/admin. |
| `Failed get service permissions for.<service>` | 400 | The service row could not be loaded (wrong or missing id). |
| `Initial service admin_id and owner_id should be equal to that of provider_id` | 400 | Creating a service with ids that are not yours. |
| `You are not allowed to create a service, you are not a provider.` | 400 | A *user* tried to create a service. |

## Data model

```
providers ─┐                         ┌─ pharmacies / laboratories / radiologycenters (owner_id, admin_id, staff)
           ├─ owner_id / admin_id ───┤
users      │                         └─ clinics ─┬─ patients ─┬─ patients_health   (id = patient id, 1:1)
 │         │                                     │            ├─ patients_drugs    (id = patient id, 1:1)
 │         │                                     │            ├─ patients_reports  (id = patient id, 1:1)
 │         │                                     │            └─ clinics_visits ─┬─ clinics_requests      (id = visit id, 1:1)
 │         │                                     │                               ├─ clinics_prescriptions (id = visit id, 1:1)
 │         │                                     │                               └─ clinics_paidservices  (id = visit id, 1:1)
 └─────────┴──── clinics_appointments (user_id, provider_id, clinic_id)   (+ pharmacies_/laboratories_/radiologycenters_appointments)
users_sessions / providers_sessions: last_login, last_logout per account
```

- **One row per parent.** Health, drugs and reports belong to a *patient* and reuse the patient id as their own id; requests, prescriptions and paid services reuse the *visit* id. So you create each of them once and then `update` the same id.
- **Cascade.** Deleting a patient removes its health, drugs, reports and visits (and, through visits, requests, prescriptions and paid services). Deleting an account removes its appointments and session row.
- **Creation order** (delete in reverse):
  1. `providers/create` + login → provider token
  2. `users/create` + login (needed for appointments)
  3. `clinics/create` (optionally pharmacies, laboratories, radiology centers)
  4. `patients/create` with `clinic_id`
  5. `health` / `patientdrugs` / `reports` `create` with the patient id
  6. `visits/create`, then `requests` / `prescriptions` / `paidservices` `create` with the visit id
  7. `appointments/clinics/create` with clinic, user and provider ids

## Validation rules

| Field | Rule |
|---|---|
| `username` | `^[a-z][a-z0-9_]*$`: starts with a lower-case letter; then lower-case letters, digits, `_`. Unique (max 100). |
| `password` | At least 8 characters with a lower-case letter, an upper-case letter, a digit and one of `! @ # $ % ^ & *`. Stored hashed. |
| `phone` | International style, e.g. `+201299999999`. |
| `email` | `name@domain.tld`. |
| `dob` (accounts) | `dd-mm-yyyy`. |
| `gender` (accounts) | Exactly `Male` or `Female`. |
| All bodies | Every key must be a real column of the target table; required (`NOT NULL`) columns without a default must be present; empty values are refused for required text. Values that look like SQL injection are rejected. |

## Errors

Client mistakes answer `{"Message": "...", "Status": "Error"}`, server faults `"Status": "Failure"`, and 401 bodies carry only `Message`.

| HTTP | When | Body (examples) |
|---|---|---|
| **400** | Body or id invalid, required column missing, field not allowed, delete not supported | `Failed to validate request body, ...`, `Invalid id provided`, `No id provided.`, `Non-nullable column missing in data: <col>`, `Key not allowed: <key>`, `Key not found in schema: <key>`, `Delete is NOT implemented` |
| **401** | Missing/invalid token or suspended account | `failed to extract token, or not a bearer token`, `Token expired`, `Token ip address does not match user ip address`, `Client is logged out from server, Please login again.`, `Account is suspend, please contact the administrator.` |
| **403** | Not allowed to act on that record, or blacklisted IP | see *Permission errors* above; plain text `IP is blacklisted.` |
| **404** | Unknown route | `{"Message": "Endpoint not found."}` |
| **429** | Rate limit reached | plain text |
| **500** | Unexpected server or database failure | `{"Message": "...", "Status": "Failure"}` |
| **503** | IP banned by the DoS protection | plain text `IP is banned.` |

## Rate limiting and abuse protection

Every route first passes a DoS filter that tracks requests per IP.

| Setting (env var) | Default | Effect |
|---|---|---|
| `MAX_REQUESTS` | 10000 | Requests allowed per `PERIOD` before the IP is rate limited. |
| `PERIOD` | 30 s | Counting window. |
| `RL_DURATION` | 30 s | How long a rate-limited IP is answered with `429`. |
| `BAN_DURATION` | 3600 s | How long a banned IP is answered with `503`. |
| `MAX_FPS` | 100 | Maximum request fingerprints tracked. |
| `WHITELIST` / `BLACKLIST` | empty | IPs that are always allowed / always refused (`403`). |

Rate-limit, blacklist and ban replies are **plain text**, not JSON, so parse them defensively.

## Configuration (environment variables)

All settings are optional; the defaults are shown. `run_release.sh` loads an optional `.env` file.

| Group | Variables (default) |
|---|---|
| Server | `SERVER_HOST` (0.0.0.0), `SERVER_PORT` (8080), `SERVER_THREADS` (4), `SERVER_DEBUG_LEVEL` (1), `SERVER_LOG_TO_CONSOLE` (true), `SERVER_LOG_TO_FILE` (true), `SERVER_LOG_DIR` (./logs/), `SERVER_LOG_FILE` (server.log), `SERVER_UPLOAD_DIR` (./uploads/), `SERVER_NAME`, `SERVER_VERSION`, `SERVER_DESCRIPTION` |
| Database | `DB_HOST` (127.0.0.1), `DB_PORT` (5432), `DB_NAME` (postgres), `DB_USER` (postgres), `DB_PASS` (postgres), `DB_SSL` (false), `DB_MAX_CONN` (10) |
| Tokens | `TOKEN_VALIDITY` (43200 min), `TOKEN_ISSUER` (ProjectValhalla), `TOKEN_TYPE` (JWS), `TOKEN_SECRET` (**change this in production**) |
| DoS | `MAX_REQUESTS`, `PERIOD`, `RL_DURATION`, `BAN_DURATION`, `CLN_FRQ`, `MAX_FPS`, `WHITELIST`, `BLACKLIST` |
| Front end (invite links) | `FRONTEND_HOST`, `FRONTEND_PORT` (3000), `FRONTEND_INVITE_PATH` (invite) |
| E-mail daemon | `EMAIL_SENDER_HOST`, `EMAIL_SENDER_PORT` (5000), `EMAIL_SENDER_QUEUE_PATH` (/enqueue) |

Create the schema with `init.sql` (see `db_init.sh`).

## Recipes

**Log in and call an endpoint (JavaScript)**

```js
const base = 'http://127.0.0.1:8080/api/v2';
const j = (r) => r.json();
const { token } = await fetch(base + '/clients/providers/login', {
  method: 'POST', headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({ username: 'dr_amr', password: '1234@Abc!' })
}).then(j);
const clinic = await fetch(base + '/services/clinics/read', {
  method: 'POST', headers: {'Content-Type': 'application/json', Authorization: 'Bearer ' + token},
  body: JSON.stringify({ id: 1, schema: [] })
}).then(j);
```

**Same thing in Python**

```python
import requests
base = "http://127.0.0.1:8080/api/v2"
token = requests.post(f"{base}/clients/providers/login", json={"username": "dr_amr", "password": "1234@Abc!"}).json()["token"]
clinic = requests.post(f"{base}/services/clinics/read", json={"id": 1, "schema": []}, headers={"Authorization": f"Bearer {token}"}).json()
```

**Search with paging**

```bash
curl -X POST $BASE/clinic/patients/search -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"keyword":"ahmed","filter":"fullname","limit":20,"offset":0,"order_by":"id","direction":0}'
```

**Add a staff member (owner or admin)** - `staff` entries look like `"<provider id>:<power>"`; see *Permissions*.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `401 failed to extract token` | Header missing, or not exactly `Authorization: Bearer <token>`. |
| `401 Token ip address does not match` | The token was issued to another IP (proxy, VPN, different host). Log in from the machine that calls the API. |
| `401 Client is logged out` | You called `logout`; log in again. |
| `401 Account is suspend` | The account was suspended; it cannot be re-activated through the API. |
| `403 You are not allowed to manage client` | Passing another account's `id`, or a user token on a providers route (or the reverse). |
| `403 You are not a staff of this service` | Add the provider to the service `staff`, or use the owner's token. |
| `400 Invalid id provided` | `read` needs the id in the **body**; `update`/`delete` need it in the **query string**. |
| `400 Failed to validate request body, Key: [x] is not found in database schema` | A body key is not a column of the table (check spelling and nesting). |
| `400 Delete is NOT implemented` | Sub-records cannot be deleted directly; delete the parent patient or visit. |
| `429` / `503` plain text | Rate limited or banned: wait `RL_DURATION` / `BAN_DURATION` or whitelist your IP. |
| Browser shows a CORS or network error from this page | Open the page from the server itself (`/api/v2/doc`) or fix the Base URL. |

## Import into other tools

- **OpenAPI 3.0**: use the **⬇ OpenAPI** button in the guide bar (or `docs/openapi.json`) with Insomnia, Postman, Swagger Editor or any code generator.
- **Postman collection**: the **⬇ Postman** button downloads a collection with every request in dependency order, the bearer headers and tests that store ids and tokens in collection variables. Run it top to bottom with the Collection Runner. Insomnia imports the same file.
- **Real response samples**: run `SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh` and rebuild the docs; the 200 examples then show real server responses (tokens are replaced by `<jwt>`).

## Testing tools

- **Benchmark** (purple button, bottom right, below the test console): measures requests/s and p50/p90/p95/p99 latency for one endpoint, or for all 89 test steps in one table. Reads and searches are repeated as they are; updates, activate, login and the not-implemented deletes are repeated; creates make a new record with unique dummy data on every request and the matching delete row is the clean-up (timed too); logout closes the sessions that were just opened; health, drugs, reports, requests, prescriptions and paid services each get a fresh patient or visit. Suspend only runs when ticked, because suspended accounts cannot be deleted through the API. Untick *Delete what was written afterwards* to keep the data, or *Include write endpoints* to only read. Times are measured in the browser, so they include network time; the panel pauses to stay under the DoS filter limit (10 000 requests / 30 s by default).
- **Test console** (button bottom right, above Benchmark): runs every endpoint in order with dummy data, shows request and response, copies curl.
- **`test_api.sh`**: the same flow with curl (89 checks). Payloads live in `api_tests/payloads/**` as JSON templates; use `VERBOSE=1` to see responses and `SKIP_SUSPEND=1` to avoid leaving suspended accounts.
- **Try it out** on any operation below: use **Authorize** (top right) to paste a token first.

## Documentation changelog

| Date | Change |
|---|---|
| 2026-11 | API guide panel, permissions explained, full error reference, data model, validation rules, configuration, recipes. |
| 2026-11 | Test console: run every endpoint with dummy data from this page. |
| 2026-11 | Try it out reuses ids returned by earlier responses (provider, user, clinic, patient, visit, ...) in place of the example ids, written into the editable body and parameter boxes; shown as chips in the header. |
| 2026-11 | Authorize fills itself from a successful login (and clears on logout). |
| 2026-11 | Benchmark panel: latency percentiles and throughput for every endpoint, writes included, with automatic clean-up. |
| 2026-11 | OpenAPI spec and Postman collection generated from the same source as the page (`tools/docs`). |
