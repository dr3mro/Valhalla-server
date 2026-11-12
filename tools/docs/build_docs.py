#!/usr/bin/env python3
"""Generate docs/openapi.json and a self-contained docs/api.html (Swagger UI inlined) for Valhalla-server."""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from steps import build_steps


def write_if_changed(path, text):
    """Leave the file (and its mtime) alone when the content is identical, so CMake does not reconfigure needlessly."""
    try:
        if open(path, encoding="utf-8").read() == text:
            return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(os.path.join(HERE, "..", ".."))
PAYLOADS = os.path.join(PROJECT, "api_tests", "payloads")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(PROJECT, "docs")
UI = os.path.join(HERE, "node_modules", "swagger-ui-dist")
SAMPLES = os.path.join(PROJECT, "api_tests", "samples.jsonl")  # optional: written by SAMPLES_OUT=... ./test_api.sh

SAMPLE = {  # values substituted into the {{PLACEHOLDER}}s of the payload templates
    "RUN": "1001", "PROVIDER_USERNAME": "dr_amr", "PROVIDER_PASSWORD": "1234@Abc!", "USER_USERNAME": "user1",
    "USER_PASSWORD": "123Mm@p0", "LOGIN_USERNAME": "dr_amr", "LOGIN_PASSWORD": "1234@Abc!", "DISPOSABLE_NAME": "temp_account",
    "PROVIDER_ID": "1000", "USER_ID": "1000", "CLINIC_ID": "1000", "PATIENT_ID": "100000", "VISIT_ID": "100000",
    "APPOINTMENT_ID": "1000", "ID": "1000",
}

def payload(name, **override):
    text = open(os.path.join(PAYLOADS, name + ".json")).read()
    vals = {**SAMPLE, **{k: str(v) for k, v in override.items()}}
    for k, v in vals.items():
        text = text.replace("{{%s}}" % k, v)
    return json.loads(text)

def _parse_init_sql(path):
    """Parse init.sql's CREATE TABLE statements into {table: {not_null, fk, has_default}}.
    Used to tell genuinely-required columns (NOT NULL, no DEFAULT) and association ids
    (REFERENCES, no DEFAULT) apart from columns the database will happily default for us -
    so the generated schemas' "required" lists match what the server actually demands,
    not every key the example payload happens to send.
    """
    try:
        sql = open(path, encoding="utf-8").read()
    except OSError:
        return {}
    tables = {}
    for m in re.finditer(r"CREATE TABLE(?:\s+IF NOT EXISTS)?\s+(\w+)\s*\((.*?)\n\);", sql, re.S):
        tname, body = m.group(1), m.group(2)
        not_null, fk, has_default = set(), set(), set()
        for line in body.split("\n"):
            line = line.strip().rstrip(",")
            if not line:
                continue
            colm = re.match(r"(\w+)\s+", line)
            if not colm:
                continue
            col = colm.group(1)
            up = line.upper()
            if col == "id" or up.startswith(("PRIMARY KEY", "FOREIGN KEY", "UNIQUE", "CHECK")):
                continue
            has_def = "DEFAULT" in up
            if has_def:
                has_default.add(col)
            if "REFERENCES" in up and not has_def:
                fk.add(col)
            if "NOT NULL" in up and not has_def:
                not_null.add(col)
        tables[tname] = {"not_null": not_null, "fk": fk, "has_default": has_default}
    return tables

TABLES = _parse_init_sql(os.path.join(PROJECT, "init.sql"))

# The six "one JSONB blob per parent record" tables: their sole payload column has a SQL DEFAULT
# (so the NOT-NULL/REFERENCES rule below would call it optional) but it's the entire reason the
# endpoint exists, so it's always required on create/update regardless of the column's default.
SOLE_PAYLOAD_COLUMN = {
    "patients_health": "health", "patients_drugs": "drugs", "patients_reports": "reports",
    "clinics_paidservices": "paidservices", "clinics_requests": "requests", "clinics_prescriptions": "prescriptions",
}

# Maps every table-backed request-body schema name to the table whose columns decide which of its
# keys are genuinely required. Schemas not listed here (Login, Search, ReadRequest, Staff...) keep
# the "every example key is required" behaviour from infer(), since they aren't a direct column mirror.
SCHEMA_TABLE = {
    "ProvidersCreate": "providers", "ProvidersUpdate": "providers",
    "UsersCreate": "users", "UsersUpdate": "users",
    "ClinicsCreate": "clinics", "ClinicsUpdate": "clinics",
    "PharmaciesCreate": "pharmacies", "PharmaciesUpdate": "pharmacies",
    "LaboratoriesCreate": "laboratories", "LaboratoriesUpdate": "laboratories",
    "RadiologycentersCreate": "radiologycenters", "RadiologycentersUpdate": "radiologycenters",
    "PatientCreate": "patients", "PatientUpdate": "patients",
    "HealthCreate": "patients_health", "HealthUpdate": "patients_health",
    "PatientdrugsCreate": "patients_drugs", "PatientdrugsUpdate": "patients_drugs",
    "ReportsCreate": "patients_reports", "ReportsUpdate": "patients_reports",
    "VisitCreate": "clinics_visits", "VisitUpdate": "clinics_visits",
    "RequestsCreate": "clinics_requests", "RequestsUpdate": "clinics_requests",
    "PrescriptionsCreate": "clinics_prescriptions", "PrescriptionsUpdate": "clinics_prescriptions",
    "PaidservicesCreate": "clinics_paidservices", "PaidservicesUpdate": "clinics_paidservices",
    "AppointmentCreate": "clinics_appointments", "AppointmentUpdate": "clinics_appointments",
}

def required_keys_for_table(table, keys):
    """Of an example body's keys, return the ones the SQL schema (or the sole-payload-column
    override above) actually demands: the record's own id, a NOT-NULL column with no DEFAULT,
    or a REFERENCES column with no DEFAULT (an association id - nullable in SQL, but not a
    request anyone would actually make without it)."""
    info = TABLES.get(table, {})
    not_null, fk = info.get("not_null", set()), info.get("fk", set())
    sole = SOLE_PAYLOAD_COLUMN.get(table)
    return [k for k in keys if k == "id" or k in not_null or k in fk or k == sole]

FIELD_DOC = {
    "avatar": "URL of the profile picture.", "active": "Whether the account is active on creation.",
    "language": "Preferred language code, e.g. `en`.", "username": "Unique login name.", "password": "Account password (plain text over TLS; stored hashed).",
    "fullname": "Full display name.", "phone": "Phone number in international format, e.g. `+201299999999`.", "email": "E-mail address.",
    "country": "Country name.", "city": "City name.", "address": "Street address.", "dob": "Date of birth, format `dd-mm-yyyy`.",
    "gender": "Gender, e.g. `Male` / `Female`.", "position": "Professional position, e.g. `Doctor`.", "speciality": "Medical speciality.",
    "specialities": "List of specialities offered.", "parent": "Id of the parent/guardian account as a string, `\"0\"` for none.",
    "marital": "Marital status, e.g. `single`.", "job": "Occupation.", "services": "Map of service type to service id the provider works with.",
    "logo": "URL of the logo image.", "name": "Display name.", "admin_id": "Provider id of the service administrator.",
    "owner_id": "Provider id of the service owner.", "staff": "Staff members grouped by role. Entries look like `\"<provider id>[:<extra>]\"` as in the sample collection.",
    "clinic_id": "Id of the clinic the record belongs to.", "id": "Id of the parent record (patient for health/drugs/reports, visit for requests/prescriptions/paid services).",
    "schema": "Names of the fields to return. The samples always send an empty array.", "keyword": "Text to look for.",
    "filter": "Column the keyword is matched against.", "limit": "Maximum number of rows to return.", "offset": "Number of rows to skip (paging).",
    "order_by": "Column to sort by.", "direction": "Sort direction (samples use `0`).", "patient_id": "Id of the patient.",
    "visit_date": "Visit date, `yyyy-mm-dd`.", "visit_time": "Visit time, `hh:mm`.", "complaints": "Chief complaints.", "symptoms": "Reported symptoms.",
    "examination": "Free-form object of examination findings (vital signs etc.).", "diagnosis": "Diagnosis text.", "comments": "Free-text comments.",
    "health": "Object of health-history answers (the sample sends `\"No\"` for each key).", "drugs": "List of drug entries.", "reports": "List of report entries.",
    "requests": "List of requested investigations.", "prescriptions": "List of prescribed drugs.", "paidservices": "List of paid services.",
    "provider_id": "Provider id.", "user_id": "User id.", "appointment_date": "Date, `yyyy-mm-dd`.", "appointment_time": "Time, `hh:mm:ss`.",
    "appointment_duration": "Duration in minutes.", "appointment_reason": "Reason for the appointment.", "appointment_note": "Notes.",
    "appointment_type": "e.g. `regular`, `online`.", "appointment_status": "e.g. `pending`.", "nominee_id": "Id of the person being invited/added.",
    "entity_id": "Id of the clinic/pharmacy/laboratory/radiology center.", "role": "Role the nominee gets in the entity.",
    "nominee_group": "Account group of the nominee: `providers` or `users`.", "entity_name": "Name of the entity (used in the invite e-mail).",
    "payload": "Staff data object.",
}

def infer(value, key=None):
    if isinstance(value, bool): s = {"type": "boolean"}
    elif isinstance(value, int): s = {"type": "integer"}
    elif isinstance(value, float): s = {"type": "number"}
    elif isinstance(value, str): s = {"type": "string"}
    elif isinstance(value, list):
        s = {"type": "array", "items": infer(value[0]) if value else {}}
    elif isinstance(value, dict):
        s = {"type": "object", "properties": {k: infer(v, k) for k, v in value.items()}}
        if value:
            # Every example body here is a real, accepted request - its keys are what the server
            # actually requires. Without this, Schemathesis (and any other schema-driven client)
            # treats "{}" as a schema-valid request for every endpoint, since nothing says
            # otherwise, and flags the server's very reasonable "Data is empty" 400 as a bug.
            s["required"] = list(value.keys())
    else:
        s = {}
    if key and key in FIELD_DOC:
        s["description"] = FIELD_DOC[key]
    return s

schemas = {
    "Error": {"type": "object", "properties": {"Message": {"type": "string"}, "Status": {"type": "string", "example": "Error"}},
              "description": "Error body. `Status` is `Error` for client mistakes and `Failure` for server faults; 401 bodies carry only `Message`."},
    "CreatedId": {"type": "object", "properties": {"id": {"type": "integer", "format": "int64"}}, "required": ["id"]},
    "LoginResponse": {"type": "object", "properties": {"token": {"type": "string", "description": "JWT, send as `Authorization: Bearer <token>`."},
                      "client_id": {"type": "integer", "format": "int64", "description": "Id of the logged-in account."}}, "required": ["token", "client_id"]},
}

paths, tag_list = {}, []
BASE_URL = "http://127.0.0.1:8080/api/v2"

def curl_sample(method, path, body, secured, query=None):
    url = BASE_URL + path + (("?" + query) if query else "")
    lines = ["curl -sS -X %s '%s'" % (method, url)]
    if secured: lines.append('  -H "Authorization: Bearer $TOKEN"')
    if body is not None:
        lines.append("  -H 'Content-Type: application/json'")
        lines.append("  -d '%s'" % json.dumps(body, ensure_ascii=False, separators=(",", ":")))
    return "```bash\n" + " \\\n".join(lines) + "\n```"

def _ex(msg, status=None):
    d = {"Message": msg}
    if status: d["Status"] = status
    return {"value": d}
_J = lambda ex: {"application/json": {"schema": {"$ref": "#/components/schemas/Error"}, "examples": ex}}
ERR400 = {"description": "**Bad request** - the body or an id is wrong, or the operation is not implemented. Body: `{\"Message\", \"Status\": \"Error\"}`.",
          "content": _J({
              "invalidBody": {"summary": "Body failed validation", **_ex("Failed to validate request body, Key: [visits] is not found in database schema.", "Error")},
              "badJson": {"summary": "Not valid JSON", **_ex("Failed to parse body.", "Error")},
              "missingId": {"summary": "Id missing or wrong", **_ex("Invalid id provided", "Error")},
              "noId": {"summary": "Id missing (query string)", **_ex("No id provided.", "Error")},
              "notProvider": {"summary": "Only providers can create services", **_ex("You are not allowed to create a service, you are not a provider.", "Error")},
              "required": {"summary": "Required column missing", **_ex("Non-nullable column missing in data: patient_id", "Error")},
              "unknownKey": {"summary": "Field not allowed", **_ex("Key not allowed: foo", "Error")},
              "empty": {"summary": "Empty value", **_ex("Value is empty for key username", "Error")},
              "notImpl": {"summary": "Delete not supported", **_ex("Delete is NOT implemented", "Error")},
              "sqli": {"summary": "Injection pattern in a value", **_ex("A Sql Injection pattern is detected in generated query.", "Error")},
          })}
ERR401 = {"description": "**Unauthorized** - the token is missing, malformed, expired, revoked, bound to another IP, or the account is suspended. Body: `{\"Message\"}` only (no `Status`).",
          "content": _J({
              "noToken": {"summary": "No / wrong Authorization header", **_ex("failed to extract token, or not a bearer token")},
              "expired": {"summary": "Token expired", **_ex("Token expired")},
              "ip": {"summary": "Used from another IP", **_ex("Token ip address does not match user ip address")},
              "loggedOut": {"summary": "After logout", **_ex("Client is logged out from server, Please login again.")},
              "suspended": {"summary": "Suspended account", **_ex("Account is suspend, please contact the administrator.")},
              "badToken": {"summary": "Corrupt token", **_ex("Token verification failed: ...")},
          })}
ERR403 = {"description": "**Forbidden** - the caller is authenticated but is not allowed to do this, or the IP is blacklisted (plain-text body `IP is blacklisted.`). "
                         "Permission errors come from the gatekeeper with a JSON body.",
          "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Error"}, "examples": {
              "notOwner": {"summary": "Acting on another client", **_ex("You are not allowed to manage client with id: 1000 and group: providers", "Error")},
              "notStaff": {"summary": "Not staff of the service", **_ex("You are not a staff of this service", "Error")},
          }}, "text/plain": {"schema": {"type": "string"}, "example": "IP is blacklisted."}}}
ERR429 = {"description": "**Too many requests** - rate limit reached for your IP. Plain-text body (not JSON). Slow down and retry.",
          "content": {"text/plain": {"schema": {"type": "string"}, "example": "IP is blacklisted."}}}
ERR500 = {"description": "**Internal error** - unexpected server or database failure. Body: `{\"Message\", \"Status\": \"Failure\"}`. The 503 `IP is banned.` and the filter's `Server error.` are plain text.",
          "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Error"}, "example": {"Message": "Failed to create Sql create statement for table patients.", "Status": "Failure"}}}}
ERR503 = {"description": "**Service unavailable** - your IP was banned by the DoS protection. Plain-text body `IP is banned.`",
          "content": {"text/plain": {"schema": {"type": "string"}, "example": "IP is banned."}}}
ERRDEF = {"description": "Any other error. JSON errors use `{\"Message\": ...}`.",
          "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Error"}}}}

def add(tag, method, path, summary, description, *, body=None, schema_name=None, secured=True, query=None, ok=None, ok_desc="Success.",
        ok_example=None, responses_extra=None, id_param=None):
    if tag not in tag_list: tag_list.append(tag)
    op = {"tags": [tag], "summary": summary, "operationId": re.sub(r"[^A-Za-z0-9]+", "_", method + path).strip("_")}
    desc = description.strip()
    desc += "\n\n**Example**\n\n" + curl_sample(method.upper(), path, body, secured, query)
    op["description"] = desc
    if not secured: op["security"] = []
    params = []
    if id_param:
        name, doc = id_param
        params.append({"name": name, "in": "query", "required": True, "schema": {"type": "integer", "format": "int64"}, "description": doc, "example": 1000})
    if params: op["parameters"] = params
    if body is not None:
        sname = schema_name
        schemas[sname] = infer(body)
        # Only the top-level body keys are validated against a fixed list by the server; nested
        # JSONB blobs (health, staff, specialities, examination...) are opaque and accept any keys.
        schemas[sname]["additionalProperties"] = False
        if sname in SCHEMA_TABLE:
            # Override infer()'s blanket "every key is required" with the real required/optional
            # split derived from init.sql for this table (see required_keys_for_table above).
            req = required_keys_for_table(SCHEMA_TABLE[sname], body.keys())
            if req:
                schemas[sname]["required"] = req
            else:
                # No individual field is required, but the server still rejects a literal "{}"
                # body ("Data is empty") - minProperties says the same thing without naming a
                # specific key (e.g. VisitUpdate, where every column is nullable with no default).
                schemas[sname].pop("required", None)
                schemas[sname]["minProperties"] = 1
        op["requestBody"] = {"required": True, "content": {"application/json": {"schema": {"$ref": "#/components/schemas/" + sname}, "example": body}}}
    r200 = {"description": ok_desc}
    if ok:
        r200["content"] = {"application/json": {"schema": ok, **({"example": ok_example} if ok_example is not None else {})}}
    responses = {"200": r200, "400": ERR400, **({"401": ERR401, "403": ERR403} if secured else {}), "429": ERR429, "500": ERR500, "503": ERR503, "default": ERRDEF}
    if responses_extra: responses.update(responses_extra)
    op["responses"] = responses
    paths.setdefault(path, {})[method] = op

ref = lambda n: {"$ref": "#/components/schemas/" + n}
CREATED = dict(ok=ref("CreatedId"), ok_desc="Created. Returns the new record id.", ok_example={"id": 1000})
READ_OK = dict(ok={"type": "object", "additionalProperties": True}, ok_desc="The record as a JSON object.")
LIST_OK = dict(ok={"oneOf": [{"type": "object"}, {"type": "array", "items": {}}]}, ok_desc="Matching records.")
NOTE_ID = ("id", "Id of the record to operate on.")

# ---------------------------------------------------------------- System
add("System", "get", "/hello", "Health check",
    "Public endpoint. Returns a welcome message; use it to check the server is up and reachable.", secured=False,
    ok={"type": "object", "properties": {"Message": {"type": "string"}}}, ok_example={"Message": "Welcome to the API!"})

# ---------------------------------------------------------------- Providers / Users
for grp, tag, label in (("providers", "Providers", "provider (healthcare professional)"), ("users", "Users", "user (patient-side account)")):
    base = "/clients/" + grp
    cap = grp.capitalize()
    add(tag, "post", base + "/create", "Register a " + label,
        "Creates a new account. **No token needed.** Usernames must be unique. The returned `id` is the account id.\n\n"
        "Password rules are enforced by the validator (the samples use mixed case, a digit and a symbol).",
        body=payload(grp + "/create"), schema_name=cap + "Create", secured=False, **CREATED)
    add(tag, "post", base + "/login", "Log in",
        "Exchanges username and password for a JWT. The token is bound to the client IP and expires (see `TOKEN_VALIDITY`). "
        "Suspended accounts cannot log in (401).", body=payload("auth/login"), schema_name=cap + "Login", secured=False,
        ok=ref("LoginResponse"), ok_desc="Authenticated.", ok_example={"token": "eyJhbGciOiJIUzI1NiIs...", "client_id": 1000})
    add(tag, "post", base + "/logout", "Log out",
        "Invalidates the current token (sent in the `Authorization` header). The body is empty.", ok_desc="Logged out.")
    add(tag, "post", base + "/read", "Read an account",
        "Returns the account named in the body. `schema` selects which fields to return.", body=payload("common/read", ID=1000), schema_name="ReadRequest", **READ_OK)
    add(tag, "put", base + "/update", "Update an account",
        "Replaces the editable fields of the account. The account id goes in the **query string**, not the body. "
        "Username and password cannot be changed here.", body=payload(grp + "/update"), schema_name=cap + "Update",
        query="id=1000", id_param=NOTE_ID)
    add(tag, "delete", base + "/delete", "Delete an account", "Permanently deletes the account `id`.", query="id=1000", id_param=NOTE_ID)
    add(tag, "post", base + "/search", "Search accounts",
        "Case-insensitive match of `keyword` against column `filter`, paged with `limit`/`offset`, sorted by `order_by`.",
        body=payload(grp + "/search"), schema_name=cap + "Search", **LIST_OK)
    add(tag, "get", base + "/services", "List the services of an account",
        "Lists the services (clinics, pharmacies, ...) linked to the account `id`.", query="id=1000", id_param=NOTE_ID,
        ok={"type": "object", "additionalProperties": True}, ok_desc="Services grouped by type.")
    add(tag, "post", base + "/activate", "Activate an account", "Marks account `id` active. No body.", query="id=1000", id_param=NOTE_ID)
    add(tag, "post", base + "/suspend", "Suspend an account",
        "Marks account `id` suspended. **A suspended account is rejected on every endpoint (401 `Account is suspend`), including "
        "`activate`, `logout` and `delete`**, so it cannot reactivate itself.", query="id=1000", id_param=NOTE_ID)

# ---------------------------------------------------------------- Services
SVC = (("clinics", "Clinics", "clinic"), ("pharmacies", "Pharmacies", "pharmacy"), ("laboratories", "Laboratories", "laboratory"),
       ("radiologycenters", "Radiology centers", "radiology center"))
for typ, tag, label in SVC:
    base = "/services/" + typ
    cap = tag.replace(" ", "")
    add(tag, "post", base + "/create", "Create a " + label,
        "Creates a %s owned/administered by the given providers. `admin_id` and `owner_id` must be existing provider ids; "
        "`staff` lists members per role. Returns the new id." % label, body=payload(typ + "/create"), schema_name=cap + "Create", **CREATED)
    add(tag, "post", base + "/read", "Read a " + label, "Returns the %s with the given `id`." % label,
        body=payload("common/read", ID=1000), schema_name="ReadRequest", **READ_OK)
    add(tag, "put", base + "/update", "Update a " + label, "Replaces the editable fields. The id goes in the query string.",
        body=payload(typ + "/update"), schema_name=cap + "Update", query="id=1000", id_param=NOTE_ID)
    add(tag, "delete", base + "/delete", "Delete a " + label, "Deletes the %s `id`." % label, query="id=1000", id_param=NOTE_ID)
    add(tag, "post", base + "/search", "Search " + tag.lower(), "Paged search; see the Providers search for the parameters.",
        body=payload(typ + "/search"), schema_name=cap + "Search", **LIST_OK)

# ---------------------------------------------------------------- Patients
T = "Patients"
add(T, "post", "/clinic/patients/create", "Register a patient",
    "Creates a patient in the clinic `clinic_id` (the clinic must exist). Returns the patient id used by health, drugs, reports and visits.",
    body=payload("patients/create"), schema_name="PatientCreate", **CREATED)
add(T, "post", "/clinic/patients/read", "Read a patient", "Returns the patient `id`.", body=payload("common/read", ID=100000), schema_name="ReadRequest", **READ_OK)
add(T, "put", "/clinic/patients/update", "Update a patient", "Id in the query string.", body=payload("patients/update"), schema_name="PatientUpdate",
    query="id=100000", id_param=NOTE_ID)
add(T, "delete", "/clinic/patients/delete", "Delete a patient",
    "One of only two clinic records that can be deleted (the other is a visit). Health, drugs and reports of the patient are removed with it (`ON DELETE CASCADE`).",
    query="id=100000", id_param=NOTE_ID)
add(T, "post", "/clinic/patients/search", "Search patients", "Paged search.", body=payload("patients/search"), schema_name="PatientSearch", **LIST_OK)
add(T, "get", "/clinic/patients/getvisits", "List the visits of a patient", "Returns the visits that belong to the patient.",
    query="patient_id=100000", id_param=("patient_id", "Id of the patient."), ok={"oneOf": [{"type": "object"}, {"type": "array", "items": {}}]}, ok_desc="Visits of the patient.")

# ---------------------------------------------------------------- Patient records / visit records
def records(tag, seg, label, parent, parent_id, note):
    base = "/clinic/%s" % seg
    cap = "".join(w.capitalize() for w in re.split(r"[_ ]", seg))
    add(tag, "post", base + "/create", "Create " + label,
        "Creates the %s of a %s. `id` is the **%s id** (one record set per %s) and `clinic_id` its clinic. %s" % (label, parent, parent, parent, note),
        body=payload(seg + "/create"), schema_name=cap + "Create", **CREATED)
    add(tag, "post", base + "/read", "Read " + label, "`id` is the %s id." % parent, body=payload("common/read", ID=parent_id), schema_name="ReadRequest", **READ_OK)
    add(tag, "put", base + "/update", "Update " + label, "`id` in the query string is the %s id." % parent, body=payload(seg + "/update"),
        schema_name=cap + "Update", query="id=%s" % parent_id, id_param=("id", "Id of the %s." % parent))
    add(tag, "delete", base + "/delete", "Delete " + label + " (not supported)",
        "**Not implemented by design**: always answers 400 `Delete is NOT implemented for entity type ...`. "
        "The rows are removed automatically when the %s is deleted." % parent, query="id=%s" % parent_id, id_param=("id", "Id of the %s." % parent))

records("Patient records", "health", "the health history", "patient", 100000, "")
records("Patient records", "patientdrugs", "the patient's drug list", "patient", 100000, "")
records("Patient records", "reports", "the patient's reports", "patient", 100000, "")

T = "Visits"
add(T, "post", "/clinic/visits/create", "Create a visit", "Creates a visit for `patient_id` in `clinic_id`. Returns the visit id used by requests, prescriptions and paid services.",
    body=payload("visits/create"), schema_name="VisitCreate", **CREATED)
add(T, "post", "/clinic/visits/read", "Read a visit", "Returns the visit `id`.", body=payload("common/read", ID=100000), schema_name="ReadRequest", **READ_OK)
add(T, "put", "/clinic/visits/update", "Update a visit",
    "The body is validated against the `clinics_visits` table, so it must use the **same column names as create** (`complaints`, not `complaint`) "
    "and must not wrap them in a `visits` array.", body=payload("visits/update"), schema_name="VisitUpdate", query="id=100000", id_param=NOTE_ID)
add(T, "delete", "/clinic/visits/delete", "Delete a visit",
    "Deletes the visit. Its requests, prescriptions and paid services are removed with it (`ON DELETE CASCADE`).", query="id=100000", id_param=NOTE_ID)

records("Visit records", "requests", "the investigation requests", "visit", 100000, "")
records("Visit records", "prescriptions", "the prescriptions", "visit", 100000, "")
records("Visit records", "paidservices", "the paid services", "visit", 100000, "")

# ---------------------------------------------------------------- Appointments
T = "Appointments"
add(T, "post", "/appointments/clinics/create", "Create a clinic appointment",
    "Books `user_id` with `provider_id` at `clinic_id`. All three must exist. Returns the appointment id.", body=payload("appointments/create"),
    schema_name="AppointmentCreate", **CREATED)
add(T, "post", "/appointments/clinics/read", "Read an appointment", "Send the id both in the query string and in the body, as the samples do.",
    body=payload("appointments/read"), schema_name="AppointmentRead", query="id=1000", id_param=NOTE_ID, **READ_OK)
add(T, "put", "/appointments/clinics/update", "Update an appointment", "Id in the query string.", body=payload("appointments/update"),
    schema_name="AppointmentUpdate", query="id=1000", id_param=NOTE_ID)
add(T, "delete", "/appointments/clinics/delete", "Delete an appointment", "Deletes the appointment.", query="id=1000", id_param=NOTE_ID)

# ---------------------------------------------------------------- Staff (templated)
STAFF = {"nominee_id": 1001, "entity_id": 1000, "role": "Doctors", "email": "colleague@mail.com", "nominee_group": "providers", "entity_name": "Mount Sinai Clinics"}
staff_param = {"name": "serviceType", "in": "path", "required": True, "schema": {"type": "string", "enum": ["clinics", "pharmacies", "laboratories", "radiologycenters"]},
               "description": "Kind of entity."}
def staff_op(action, summary, desc, body, sname):
    path = "/services/{serviceType}/staff/" + action
    add("Staff", "post", path.replace("{serviceType}", "clinics"), summary, desc + "\n\n_Not exercised by `test_api.sh`; payload taken from the controller source._",
        body=body, schema_name=sname)
    # re-point the example path to the template with an enum parameter
    op = paths.pop(path.replace("{serviceType}", "clinics"))["post"]
    op["parameters"] = [staff_param]
    op["operationId"] = "post_staff_" + action
    paths[path] = {"post": op}
staff_op("invite", "Invite someone to the staff",
         "Sends an invitation e-mail (through the e-mail sender daemon) with a link the nominee follows to join the entity. Requires permission to manage the entity's staff.", STAFF, "StaffInvite")
staff_op("add", "Add a staff member", "Adds the nominee described in `payload` to the entity's staff.", {"payload": STAFF}, "StaffAdd")
staff_op("remove", "Remove a staff member", "Removes the nominee described in `payload`. `id` is the entity id used for the permission check.", {"id": 1000, "payload": STAFF}, "StaffRemove")

# ---------------------------------------------------------------- document
GUIDE_MD = open(os.path.join(HERE, "guide.md"), encoding="utf-8").read()
INTRO = ("REST API of the **Valhalla** healthcare backend (Drogon / C++20, PostgreSQL). Base URL: `http://<host>:<port>/api/v2` (default `http://127.0.0.1:8080/api/v2`).\n\n"
         "Open the **API Guide** (blue button, bottom right) for authentication, permissions, data model, validation, errors, configuration and recipes, "
         "and the **Test console** (green button) to run every endpoint with dummy data.")

VC_STEPS = build_steps(PAYLOADS)
_first = {}
for _s in VC_STEPS:
    if _s["optional"] or _s["cleanup"]: continue
    _first.setdefault((_s["method"].lower(), _s["path"]), _s["id"])
for _p, _ops in paths.items():
    for _m, _op in _ops.items():
        _id = _first.get((_m, _p))
        if _id:
            _op["description"] += '\n\n<a href="#vc:%s">&#9654; Run this endpoint with dummy data</a>' % _id

# ---- real response samples (optional): captured by  SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh
def _scrub(v):
    if isinstance(v, dict):
        return {k: ("<jwt>" if k == "token" else _scrub(x)) for k, x in v.items()}
    if isinstance(v, list):
        return [_scrub(x) for x in v[:3]]
    return v
n_samples = 0
if os.path.exists(SAMPLES):
    seen = set()
    for line in open(SAMPLES, encoding="utf-8"):
        try:
            r = json.loads(line)
            key = (r["method"].lower(), r["path"].split("?")[0])
            if r["status"] != 200 or key in seen or key not in {(m, p) for p, o in paths.items() for m in o}:
                continue
            ex = _scrub(json.loads(r["body"]))
        except Exception:
            continue
        seen.add(key)
        r200 = paths[key[1]][key[0]]["responses"]["200"]
        r200.setdefault("content", {"application/json": {"schema": {"type": "object", "additionalProperties": True}}})
        r200["content"]["application/json"]["example"] = ex
        n_samples += 1
print("real response samples used:", n_samples)

# ---- Postman collection generated from the same steps as the test console
def postman(steps):
    folders = {}
    for s in steps:
        item = {"name": s["name"], "request": {"method": s["method"], "header": [], "url": {"raw": "{{baseUrl}}" + s["path"] + (("?" + s["query"]) if s["query"] else ""),
                "host": ["{{baseUrl}}"], "path": [x for x in s["path"].split("/") if x], "query": ([{"key": k, "value": v} for k, v in (q.split("=", 1) for q in s["query"].split("&"))] if s["query"] else [])}}}
        if s["auth"]:
            item["request"]["header"].append({"key": "Authorization", "value": "Bearer {{%s}}" % s["auth"]})
        if s["body"] and s["body"] != "@empty":
            item["request"]["header"].append({"key": "Content-Type", "value": "application/json"})
            item["request"]["body"] = {"mode": "raw", "raw": s["body"], "options": {"raw": {"language": "json"}}}
        pre = ['pm.collectionVariables.set("%s", %s);' % (k, "pm.collectionVariables.get('%s')" % re.fullmatch(r"\{\{(\w+)\}\}", v).group(1)) for k, v in s["set"].items()]
        ev = []
        if pre: ev.append({"listen": "prerequest", "script": {"type": "text/javascript", "exec": pre}})
        tests = ['pm.test("status is %d", function () { pm.response.to.have.status(%d); });' % (s["expect"], s["expect"])]
        for var, key in s["saves"].items():
            tests.append('if (pm.response.code === %d) { pm.collectionVariables.set("%s", String(pm.response.json()["%s"])); }' % (s["expect"], var, key))
        ev.append({"listen": "test", "script": {"type": "text/javascript", "exec": tests}})
        item["event"] = ev
        folders.setdefault(s["section"], []).append(item)
    consts = {"baseUrl": "http://127.0.0.1:8080/api/v2", "PROVIDER_PASSWORD": "1234@Abc!", "USER_PASSWORD": "123Mm@p0"}
    return {"info": {"name": "Valhalla Server API v2", "description": "Run the folders top to bottom (Collection Runner). Generated from docs/api.html.",
                      "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"},
            "event": [{"listen": "prerequest", "script": {"type": "text/javascript", "exec": [
                "if (!pm.collectionVariables.get('RUN')) {",
                "  const run = String(Date.now()).slice(-6);",
                "  pm.collectionVariables.set('RUN', run);",
                "  pm.collectionVariables.set('PROVIDER_USERNAME', 'provider' + run);",
                "  pm.collectionVariables.set('USER_USERNAME', 'user' + run);",
                "  pm.collectionVariables.set('DISP_PROVIDER', 'providerx' + run);",
                "  pm.collectionVariables.set('DISP_USER', 'userx' + run);",
                "}"]}}],
            "variable": [{"key": k, "value": v} for k, v in consts.items()],
            "item": [{"name": n, "item": it} for n, it in folders.items()]}
POSTMAN = postman(VC_STEPS)
os.makedirs(OUT, exist_ok=True)
write_if_changed(os.path.join(OUT, "valhalla.postman_collection.json"), json.dumps(POSTMAN, indent=2))

spec = {
    "openapi": "3.0.3",
    "info": {"title": "Valhalla Server API", "version": "v2", "description": INTRO.strip()},
    "servers": [{"url": "{scheme}://{host}:{port}/api/v2", "variables": {"scheme": {"default": "http", "enum": ["http", "https"]},
                                                                           "host": {"default": "127.0.0.1"}, "port": {"default": "8080"}}}],
    "tags": [{"name": t} for t in tag_list],
    "paths": paths,
    "components": {"schemas": schemas, "securitySchemes": {"bearerAuth": {"type": "http", "scheme": "bearer", "bearerFormat": "JWT", "description": "The JWT returned by a login endpoint. **Run a login with Try it out and this fills in by itself**; you can also paste a token here."}}},
    "security": [{"bearerAuth": []}],
}
tag_descr = {
    "System": "Liveness check.", "Providers": "Accounts for healthcare professionals.", "Users": "Patient-side accounts.",
    "Clinics": "Clinics (the service patients and visits belong to).", "Pharmacies": "Pharmacy services.", "Laboratories": "Laboratory services.",
    "Radiology centers": "Radiology services.", "Patients": "Patients registered in a clinic.",
    "Patient records": "One record set per patient. `id` is the patient id. Delete is not supported (cascade from the patient).",
    "Visits": "Visits of a patient.", "Visit records": "One record set per visit. `id` is the visit id. Delete is not supported (cascade from the visit).",
    "Appointments": "Bookings between a user and a provider at a clinic.", "Staff": "Manage who works for a clinic, pharmacy, laboratory or radiology center.",
}
PERM = {
    "Providers": "**Permissions:** `create` and `login` are public. `read`, `update`, `delete`, `activate`, `suspend`, `services` and `logout` work only on **your own** account (`403` otherwise). `search` needs any valid token.",
    "Users": "**Permissions:** `create` and `login` are public. `read`, `update`, `delete`, `activate`, `suspend`, `services` and `logout` work only on **your own** account (`403` otherwise). `search` needs any valid token.",
    "Clinics": "**Permissions:** `create` - a provider, with `owner_id` and `admin_id` equal to their own id. `read` - owner, admin or staff with `CAN_READ` (`0x04`); `update` - owner, admin or staff with `CAN_WRITE` (`0x08`); `delete` - **owner only**.",
    "Pharmacies": "**Permissions:** same as clinics: create = provider (own id as owner and admin); read = staff `CAN_READ` (`0x04`); update = staff `CAN_WRITE` (`0x08`); delete = owner only. Owner and admin always pass.",
    "Laboratories": "**Permissions:** same as clinics: create = provider (own id as owner and admin); read = staff `CAN_READ` (`0x04`); update = staff `CAN_WRITE` (`0x08`); delete = owner only. Owner and admin always pass.",
    "Radiology centers": "**Permissions:** same as clinics: create = provider (own id as owner and admin); read = staff `CAN_READ` (`0x04`); update = staff `CAN_WRITE` (`0x08`); delete = owner only. Owner and admin always pass.",
    "Patients": "**Permissions:** the owner or admin of the patient's clinic, or clinic staff with the matching bit: read `0x04`, create/update `0x08`, delete `0x10`. Columns: `clinic_id`, `fullname`, `phone`, `city`, `dob` (`yyyy-mm-dd`), `gender`, `marital`, `job` required; `email`, `address` optional.",
    "Patient records": "**Permissions:** same as patients (clinic owner, admin, or staff with the matching bit: read `0x04`, create/update `0x08`). One row per patient, keyed by the patient id; create it once, then update.",
    "Visits": "**Permissions:** clinic owner, admin, or staff with read `0x04`, create/update `0x08`, delete `0x10`. Columns: `patient_id`, `clinic_id`; optional `visit_date`, `visit_time`, `complaints`, `symptoms`, `examination` (JSON), `diagnosis`, `comments`. Deleting a visit also removes its requests, prescriptions and paid services.",
    "Visit records": "**Permissions:** same as visits. One row per visit, keyed by the visit id.",
    "Appointments": "**Permissions:** clinic owner, admin, or staff with read `0x04`, create/update `0x08`, delete `0x10`. Needs an existing `clinic_id`, `user_id` and `provider_id`. Appointment dates are `yyyy-mm-dd`, times `hh:mm:ss`.",
    "Staff": "**Permissions:** only the owner or admin of the entity. `staff` is stored as `{\"<role>\": [\"<provider id>:<power>\"]}`; the power bit mask is explained in *Permissions* above (read+write+delete = 28).",
}
for t in spec["tags"]:
    t["description"] = (tag_descr.get(t["name"], "") + ("\n\n" + PERM[t["name"]] if t["name"] in PERM else "")).strip()

os.makedirs(OUT, exist_ok=True)
write_if_changed(os.path.join(OUT, "openapi.json"), json.dumps(spec, indent=2, ensure_ascii=False))

def read(p): return open(os.path.join(UI, p), encoding="utf-8").read()
spec_js = json.dumps(spec, ensure_ascii=False).replace("</", "<\\/")

# ---- schemas section for the guide panel (Swagger's own "Schemas" block is hidden)
def _flat(sch, prefix=""):
    rows = []
    req = set(sch.get("required", []))
    for k, v in sch.get("properties", {}).items():
        name = prefix + k
        t = v.get("type", "any")
        if t == "array":
            it = v.get("items", {})
            t = "array of " + it.get("type", "any")
        if v.get("format"): t += " (%s)" % v["format"]
        rows.append((name, t, "yes" if k in req else "no", v.get("description", "")))
        if v.get("type") == "object" and v.get("properties"):
            rows += _flat(v, name + ".")
        elif v.get("type") == "array" and v.get("items", {}).get("type") == "object":
            rows += _flat(v["items"], name + "[].")
    return rows
def _schema_md():
    used, example = {}, {}
    for pth, ops in paths.items():
        for m, op in ops.items():
            rb = op.get("requestBody", {}).get("content", {}).get("application/json", {})
            r = rb.get("schema", {}).get("$ref", "")
            if r:
                n = r.rsplit("/", 1)[1]
                used.setdefault(n, []).append("`%s %s`" % (m.upper(), pth))
                example.setdefault(n, rb.get("example"))
    out = ["## Schemas", "",
           "Request and response bodies. Field types come from the sample bodies the test console sends (shown under each schema); *Required* is marked only where the schema declares it. "
           "A missing NOT NULL column is answered with `400 Non-nullable column missing in data: <col>`, so check `init.sql` for the full list.", ""]
    for name in sorted(schemas):
        sch = schemas[name]
        out += ["### " + name, ""]
        if sch.get("description"): out += [sch["description"], ""]
        if name in used: out += ["Used by: " + ", ".join(used[name]), ""]
        rows = _flat(sch)
        if rows:
            out += ["| Field | Type | Required | Description |", "|---|---|---|---|"]
            out += ["| `%s` | %s | %s | %s |" % (n, t, r, d.replace("|", "\\|")) for n, t, r, d in rows]
            out.append("")
        if example.get(name) is not None:
            out += ["```json", json.dumps(example[name], indent=2, ensure_ascii=False), "```", ""]
    return "\n".join(out) + "\n"
GUIDE_MD = GUIDE_MD.replace("## Import into other tools", _schema_md() + "## Import into other tools", 1)
import markdown as _md
guide_html = _md.markdown(GUIDE_MD, extensions=["tables", "fenced_code", "toc"], extension_configs={"toc": {"permalink": False}})
postman_js = json.dumps(POSTMAN, ensure_ascii=False).replace("</", "<\\/")
steps_js = json.dumps(VC_STEPS, ensure_ascii=False).replace("</", "<\\/")
html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Valhalla Server API</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A//www.w3.org/2000/svg%22%20viewBox%3D%220%200%2064%2064%22%3E%3Cdefs%3E%3ClinearGradient%20id%3D%22g%22%20x1%3D%220%22%20y1%3D%220%22%20x2%3D%221%22%20y2%3D%221%22%3E%3Cstop%20offset%3D%220%22%20stop-color%3D%22%234f8cff%22/%3E%3Cstop%20offset%3D%221%22%20stop-color%3D%22%232e9e6b%22/%3E%3C/linearGradient%3E%3C/defs%3E%3Cpath%20d%3D%22M32%203%2056%2012v18c0%2014-10%2026-24%2031C18%2056%208%2044%208%2030V12z%22%20fill%3D%22url%28%23g%29%22/%3E%3Cpath%20d%3D%22M20%2020h7l5%2015%205-15h7L35%2046h-6z%22%20fill%3D%22%23fff%22/%3E%3C/svg%3E">
<script>try {{ if (localStorage.getItem("vc.theme") !== "dark") document.documentElement.classList.add("vc-light"); }} catch (e) {{ document.documentElement.classList.add("vc-light"); }}</script>
<style>{read("swagger-ui.css")}</style>
<style>{open(os.path.join(HERE, "page.css"), encoding="utf-8").read()}</style>
<style>{open(os.path.join(HERE, "console.css"), encoding="utf-8").read()}</style>
<style>body{{margin:0}} .swagger-ui .info .title small.version-stamp{{display:none}} .swagger-ui .markdown pre{{white-space:pre-wrap}}</style>
</head>
<body>
<header id="vh-header"><span class="vh-logo"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4f8cff"/><stop offset="1" stop-color="#2e9e6b"/></linearGradient></defs><path d="M32 3 56 12v18c0 14-10 26-24 31C18 56 8 44 8 30V12z" fill="url(#g)"/><path d="M20 20h7l5 15 5-15h7L35 46h-6z" fill="#fff"/></svg></span><span class="vh-name">Valhalla</span><span class="vh-sub">Healthcare Server API</span><span id="vh-ids" style="display:none"></span><button id="vh-theme" type="button" title="Switch dark / light">&#9680; Theme</button><span class="vh-ver">v2</span></header>
<div id="vc-root"></div>
<div id="vb-root"></div>
<div id="vg-root"><div class="vg-backdrop" style="position:absolute;inset:0;background:rgba(0,0,0,.55)"></div>
<div class="vg-panel"><div class="vg-bar"><h2>API Guide<small>Valhalla Server v2</small></h2><div class="vc-tools">
<button class="vc-btn vg-dl" data-kind="openapi" type="button" title="Download the OpenAPI 3 spec">&#11015; OpenAPI</button><button class="vc-btn vg-dl" data-kind="postman" type="button" title="Download a Postman collection (also imports into Insomnia)">&#11015; Postman</button>
<button class="vc-icon vg-theme" type="button" title="Switch dark / light">&#9680;</button><button class="vc-icon vg-close" type="button" title="Close (Esc)">&#10005;</button></div></div>
<div class="vg-body"><nav class="vg-nav"></nav><div class="vg-content">{guide_html}</div></div></div></div>
<button id="vg-launch" type="button">&#128214; API Guide</button>
<div id="swagger-ui"></div>
<script>{read("swagger-ui-bundle.js")}</script>
<script>window.__VC_STEPS__ = {steps_js};</script>
<script>window.__VG_POSTMAN__ = {postman_js};</script>
<script>{open(os.path.join(HERE, "swagger-ids.js"), encoding="utf-8").read()}</script>
<script>
const spec = {spec_js};
window.__VG_SPEC__ = spec;
window.vhToast = vhToast;
function vhToast(text) {{
  var t = document.getElementById('vh-toast');
  if (!t) {{ t = document.createElement('div'); t.id = 'vh-toast'; document.body.appendChild(t); }}
  t.textContent = text; t.className = 'show';
  clearTimeout(vhToast.h); vhToast.h = setTimeout(function () {{ t.className = ''; }}, 3500);
}}
window.onload = () => {{
  window.ui = SwaggerUIBundle({{
    spec, dom_id: '#swagger-ui', deepLinking: true, docExpansion: 'none', defaultModelsExpandDepth: -1,
    displayRequestDuration: true, filter: true, persistAuthorization: true, tryItOutEnabled: false,
    presets: [SwaggerUIBundle.presets.apis], layout: 'BaseLayout',
    // A successful login through "Try it out" fills the Authorize button with its JWT; logout clears it again.
    requestInterceptor: function (req) {{ return window.vhIds ? window.vhIds.fill(req) : req; }},
    responseInterceptor: function (res) {{
      try {{
        if (window.vhIds) window.vhIds.capture(res);
        var login = /\/clients\/(users|providers)\/login(\?|$)/.test(res.url);
        var logout = /\/clients\/(users|providers)\/logout(\?|$)/.test(res.url);
        if (login && res.status === 200) {{
          var j = res.body && typeof res.body === 'object' ? res.body : JSON.parse(res.text);
          if (j && j.token && window.ui) {{
            window.ui.authActions.authorize({{ bearerAuth: {{ name: 'bearerAuth', schema: {{ type: 'http', scheme: 'bearer' }}, value: j.token }} }});
            vhToast('Authorized: the token from this login is now used for every endpoint.');
          }}
        }} else if (logout && res.status === 200 && window.ui) {{
          window.ui.authActions.logout(['bearerAuth']);
          vhToast('Logged out: the token was removed from Authorize.');
        }}
      }} catch (e) {{ /* never break the response */ }}
      return res;
    }}
  }});
}};
</script>
<script>{open(os.path.join(HERE, "console.js"), encoding="utf-8").read()}</script>
<script>{open(os.path.join(HERE, "guide.js"), encoding="utf-8").read()}</script>
<script>{open(os.path.join(HERE, "benchmark.js"), encoding="utf-8").read()}</script>
<script>
// Swagger shows "Cancel" after "Try it out"; label it "Close" instead.
(function () {{
  var KIND = {{ 2: ['ok', 'Success'], 3: ['redir', 'Redirect'], 4: ['client', 'Client error'], 5: ['server', 'Server error'] }};
  function colorResponses() {{
    document.querySelectorAll('#swagger-ui .live-responses-table').forEach(function (t) {{
      var cell = t.querySelector('tr.response .response-col_status');
      if (!cell) return;
      var m = (cell.firstChild && cell.firstChild.textContent || cell.textContent).trim().match(/^\d{{3}}/);
      var k = m ? KIND[m[0].charAt(0)] : null;
      var cls = 'vh-resp vh-r-' + (k ? k[0] : 'none');
      if (t.className.indexOf(cls) < 0) {{ t.className = t.className.replace(/\s*vh-resp\S*|\s*vh-r-\S+/g, '') + ' ' + cls; }}
      var label = k ? k[1] : 'No response (network or CORS error)';
      var b = cell.querySelector('.vh-badge');
      if (!b) {{ b = document.createElement('span'); b.className = 'vh-badge'; cell.appendChild(b); }}
      if (b.textContent !== label) b.textContent = label;
    }});
  }}
  function relabel() {{ colorResponses(); document.querySelectorAll('#swagger-ui .try-out__btn.cancel').forEach(function (b) {{ if (b.textContent !== 'Close') b.textContent = 'Close'; }}); }}
  new MutationObserver(relabel).observe(document.documentElement, {{ childList: true, subtree: true, characterData: true }});
}})();
</script>

</body>
</html>
"""
write_if_changed(os.path.join(OUT, "api.html"), html)
n_ops = sum(len(v) for v in paths.values())
print("paths:", len(paths), "operations:", n_ops, "schemas:", len(schemas), "html KB:", len(html) // 1024)
