-- init.sql
-- drop all data
DROP SCHEMA public CASCADE;
CREATE SCHEMA public;

-- Set starting user_id from 1000
CREATE SEQUENCE users_id_seq START 1000;

-- Set starting provider_id from 1000
CREATE SEQUENCE providers_id_seq START 1000;

-- Set starting clinics_id from 1000
CREATE SEQUENCE clinics_id_seq START 1000;

-- Set starting pharmacies_id from 1000
CREATE SEQUENCE pharmacies_id_seq START 1000;

-- Set starting laboratories_id from 1000
CREATE SEQUENCE laboratories_id_seq START 1000;

-- Set starting radiologycenters_id from 1000
CREATE SEQUENCE radiologycenters_id_seq START 1000;

-- Set starting clinics_appointments_id from 1000
CREATE SEQUENCE clinics_appointments_id_seq START 1000;

-- Set starting pharmacies_appointments_id from 1000
CREATE SEQUENCE pharmacies_appointments_id_seq START 1000;

-- Set starting laboratories_appointments_id from 1000
CREATE SEQUENCE laboratories_appointments_id_seq START 1000;

-- Set starting radiologycenters_appointments_id from 1000
CREATE SEQUENCE radiologycenters_appointments_id_seq START 1000;

-- Create the sequence starting from 100000
CREATE SEQUENCE patients_id_seq START 100000;

-- Set starting visits_id from 100000
 CREATE SEQUENCE clinics_visits_id_seq START 100000;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY DEFAULT nextval('users_id_seq'),
    avatar VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    language VARCHAR NOT NULL,
    active BOOLEAN DEFAULT TRUE,
    parent VARCHAR NOT NULL,
    username VARCHAR(100) UNIQUE NOT NULL,
    password VARCHAR NOT NULL,
    fullname VARCHAR NOT NULL,
    email VARCHAR NOT NULL,
    phone VARCHAR NOT NULL,
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    dob VARCHAR NOT NULL,
    gender VARCHAR NOT NULL,
    job VARCHAR NOT NULL,
    marital VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW users_safe AS
SELECT id, avatar, language, active, parent, fullname, email, phone, country, city, address, dob, gender, job, marital,created_on,updated_on
FROM users;


-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS providers (
    id INTEGER PRIMARY KEY DEFAULT nextval('providers_id_seq'),
    avatar VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    language VARCHAR NOT NULL,
    active BOOLEAN DEFAULT TRUE,
    username VARCHAR(100) UNIQUE NOT NULL,
    password VARCHAR NOT NULL,
    fullname VARCHAR NOT NULL,
    email VARCHAR NOT NULL,
    phone VARCHAR NOT NULL,
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    dob VARCHAR NOT NULL,
    gender VARCHAR NOT NULL,
    position VARCHAR NOT NULL,
    speciality VARCHAR NOT NULL,
    services JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW providers_safe AS
SELECT id, avatar, language, active, fullname, email, phone, country, city, address, dob, gender, position, speciality, services,created_on,updated_on
FROM providers;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS clinics (
    id INTEGER PRIMARY KEY DEFAULT nextval('clinics_id_seq'),
    admin_id INTEGER NOT NULL,
    owner_id INTEGER NOT NULL,
    logo VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    name VARCHAR NOT NULL,
    specialities JSONB DEFAULT '[]',
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    phone JSONB DEFAULT '[]',
    staff JSONB DEFAULT '{}'
);

-- Create safeview for search
CREATE VIEW clinics_safe AS
SELECT id,admin_id,owner_id,logo, name, specialities, country, city, address, phone, staff
FROM clinics;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS pharmacies (
    id INTEGER PRIMARY KEY DEFAULT nextval('pharmacies_id_seq'),
    admin_id INTEGER NOT NULL,
    owner_id INTEGER NOT NULL,
    logo VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    name VARCHAR NOT NULL,
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    phone JSONB DEFAULT '[]',
    staff JSONB DEFAULT '{}'
);

-- Create safeview for search
CREATE VIEW pharmacies_safe AS
SELECT id,admin_id,owner_id,logo, name, country, city, address, phone, staff
FROM pharmacies;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS laboratories (
    id INTEGER PRIMARY KEY DEFAULT nextval('laboratories_id_seq'),
    admin_id INTEGER NOT NULL,
    owner_id INTEGER NOT NULL,
    logo VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    name VARCHAR NOT NULL,
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    phone JSONB DEFAULT '[]',
    staff JSONB DEFAULT '{}'
);

-- Create safeview for search
CREATE VIEW laboratories_safe AS
SELECT id,admin_id,owner_id,logo, name, country, city, address, phone, staff
FROM laboratories;


-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS radiologycenters (
    id INTEGER PRIMARY KEY DEFAULT nextval('radiologycenters_id_seq'),
    admin_id INTEGER NOT NULL,
    owner_id INTEGER NOT NULL,
    logo VARCHAR DEFAULT 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
    name VARCHAR NOT NULL,
    country VARCHAR NOT NULL,
    city VARCHAR NOT NULL,
    address VARCHAR NOT NULL,
    phone JSONB DEFAULT '[]',
    staff JSONB DEFAULT '{}'
);

-- Create safeview for search
CREATE VIEW radiologycenters_safe AS
SELECT id, admin_id,owner_id,logo, name, country, city, address, phone, staff
FROM radiologycenters;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS clinics_appointments (
    id INTEGER PRIMARY KEY DEFAULT nextval('clinics_appointments_id_seq'),
    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id) ON DELETE CASCADE,
    provider_id INTEGER REFERENCES providers(id) ON DELETE CASCADE,
    appointment_date DATE NOT NULL,
    appointment_time TIME NOT NULL,
    appointment_duration INTEGER NOT NULL,
    appointment_reason VARCHAR NOT NULL,
    appointment_note VARCHAR NOT NULL,
    appointment_payment_status VARCHAR NOT NULL,
    appointment_payment_method VARCHAR NOT NULL,
    appointment_payment_amount INTEGER NOT NULL,
    appointment_payment_currency VARCHAR NOT NULL,
    appointment_payment_date DATE NOT NULL,
    appointment_payment_time TIME NOT NULL,
    appointment_payment_transaction_id VARCHAR NOT NULL,
    appointment_payment_transaction_status VARCHAR NOT NULL,
    appointment_type VARCHAR NOT NULL,
    appointment_status VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW clinics_appointments_safe AS
SELECT id, user_id,clinic_id,provider_id,appointment_date,appointment_time,appointment_duration,appointment_reason,appointment_note,appointment_payment_status,appointment_payment_method,appointment_payment_amount,appointment_payment_currency,appointment_payment_date,appointment_payment_time,appointment_payment_transaction_id,appointment_payment_transaction_status,appointment_type,appointment_status
FROM clinics_appointments;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS pharmacies_appointments (
    id INTEGER PRIMARY KEY DEFAULT nextval('pharmacies_appointments_id_seq'),
    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    pharmacy_id INTEGER REFERENCES pharmacies(id) ON DELETE CASCADE,
    provider_id INTEGER REFERENCES providers(id) ON DELETE CASCADE,
    appointment_date DATE NOT NULL,
    appointment_time TIME NOT NULL,
    appointment_duration INTEGER NOT NULL,
    appointment_reason VARCHAR NOT NULL,
    appointment_note VARCHAR NOT NULL,
    appointment_payment_status VARCHAR NOT NULL,
    appointment_payment_method VARCHAR NOT NULL,
    appointment_payment_amount INTEGER NOT NULL,
    appointment_payment_currency VARCHAR NOT NULL,
    appointment_payment_date DATE NOT NULL,
    appointment_payment_time TIME NOT NULL,
    appointment_payment_transaction_id VARCHAR NOT NULL,
    appointment_payment_transaction_status VARCHAR NOT NULL,
    appointment_type VARCHAR NOT NULL,
    appointment_status VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW pharmacies_appointments_safe AS
SELECT id, user_id,pharmacy_id,provider_id,appointment_date,appointment_time,appointment_duration,appointment_reason,appointment_note,appointment_payment_status,appointment_payment_method,appointment_payment_amount,appointment_payment_currency,appointment_payment_date,appointment_payment_time,appointment_payment_transaction_id,appointment_payment_transaction_status,appointment_type,appointment_status
FROM pharmacies_appointments;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS laboratories_appointments (
    id INTEGER PRIMARY KEY DEFAULT nextval('laboratories_appointments_id_seq'),
    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    laboratory_id INTEGER REFERENCES laboratories(id) ON DELETE CASCADE,
    provider_id INTEGER REFERENCES providers(id) ON DELETE CASCADE,
    appointment_date DATE NOT NULL,
    appointment_time TIME NOT NULL,
    appointment_duration INTEGER NOT NULL,
    appointment_reason VARCHAR NOT NULL,
    appointment_note VARCHAR NOT NULL,
    appointment_payment_status VARCHAR NOT NULL,
    appointment_payment_method VARCHAR NOT NULL,
    appointment_payment_amount INTEGER NOT NULL,
    appointment_payment_currency VARCHAR NOT NULL,
    appointment_payment_date DATE NOT NULL,
    appointment_payment_time TIME NOT NULL,
    appointment_payment_transaction_id VARCHAR NOT NULL,
    appointment_payment_transaction_status VARCHAR NOT NULL,
    appointment_type VARCHAR NOT NULL,
    appointment_status VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW laboratories_appointments_safe AS
SELECT id, user_id,laboratory_id,provider_id,appointment_date,appointment_time,appointment_duration,appointment_reason,appointment_note,appointment_payment_status,appointment_payment_method,appointment_payment_amount,appointment_payment_currency,appointment_payment_date,appointment_payment_time,appointment_payment_transaction_id,appointment_payment_transaction_status,appointment_type,appointment_status
FROM laboratories_appointments;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS radiologycenters_appointments (
    id INTEGER PRIMARY KEY DEFAULT nextval('radiologycenters_appointments_id_seq'),
    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    radiologycenter_id INTEGER REFERENCES radiologycenters(id) ON DELETE CASCADE,
    provider_id INTEGER REFERENCES providers(id) ON DELETE CASCADE,
    appointment_date DATE NOT NULL,
    appointment_time TIME NOT NULL,
    appointment_duration INTEGER NOT NULL,
    appointment_reason VARCHAR NOT NULL,
    appointment_note VARCHAR NOT NULL,
    appointment_payment_status VARCHAR NOT NULL,
    appointment_payment_method VARCHAR NOT NULL,
    appointment_payment_amount INTEGER NOT NULL,
    appointment_payment_currency VARCHAR NOT NULL,
    appointment_payment_date DATE NOT NULL,
    appointment_payment_time TIME NOT NULL,
    appointment_payment_transaction_id VARCHAR NOT NULL,
    appointment_payment_transaction_status VARCHAR NOT NULL,
    appointment_type VARCHAR NOT NULL,
    appointment_status VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);
-- Create safeview for search
CREATE VIEW radiologycenters_appointments_safe AS
SELECT id, user_id,radiologycenter_id,provider_id,appointment_date,appointment_time,appointment_duration,appointment_reason,appointment_note,appointment_payment_status,appointment_payment_method,appointment_payment_amount,appointment_payment_currency,appointment_payment_date,appointment_payment_time,appointment_payment_transaction_id,appointment_payment_transaction_status,appointment_type,appointment_status
FROM radiologycenters_appointments;

-- Create the table with correct usage of sequence for id column
CREATE TABLE IF NOT EXISTS patients (
    id INTEGER PRIMARY KEY DEFAULT nextval('patients_id_seq'),
    clinic_id INTEGER REFERENCES clinics(id),
    fullname VARCHAR NOT NULL,
    phone VARCHAR NOT NULL,
    email VARCHAR DEFAULT '',
    city VARCHAR NOT NULL,
    address VARCHAR DEFAULT '',
    dob DATE NOT NULL,
    gender VARCHAR NOT NULL,
    marital VARCHAR NOT NULL,
    job VARCHAR NOT NULL,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW patients_safe AS
SELECT *
FROM patients;

-- Create the table for patients health
CREATE TABLE IF NOT EXISTS patients_health (
    id INTEGER PRIMARY KEY REFERENCES patients(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    health JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW patients_health_safe AS
SELECT *
FROM patients_health;

-- Create the table for patients reports
CREATE TABLE IF NOT EXISTS patients_reports (
    id INTEGER PRIMARY KEY REFERENCES patients(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    reports JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW  patients_reports_safe AS
SELECT *
FROM patients_reports;

-- Create the table for patients drugs
CREATE TABLE patients_drugs (
    id INTEGER PRIMARY KEY REFERENCES patients(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    drugs JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW patients_drugs_safe AS
SELECT *
FROM patients_drugs;

-- Create the table for visits
CREATE TABLE IF NOT EXISTS clinics_visits (
    id INTEGER PRIMARY KEY DEFAULT nextval('clinics_visits_id_seq'),
    patient_id INTEGER REFERENCES patients(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id) ON DELETE CASCADE,
    visit_date DATE DEFAULT CURRENT_DATE,
    visit_time TIME DEFAULT CURRENT_TIME,
    complaints VARCHAR,
    symptoms VARCHAR,
    examination JSONB DEFAULT '{}',
    diagnosis VARCHAR,
    comments VARCHAR,
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW clinics_visits_safe AS
SELECT *
FROM clinics_visits;

-- Create the table for paidservices
CREATE TABLE IF NOT EXISTS clinics_paidservices (
    id INTEGER PRIMARY KEY REFERENCES clinics_visits(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    paidservices JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW clinics_paidservices_safe AS
SELECT *
FROM clinics_paidservices;

-- Create the table for requests
CREATE TABLE IF NOT EXISTS clinics_requests (
    id INTEGER PRIMARY KEY REFERENCES clinics_visits(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    requests JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW clinics_requests_safe AS
SELECT *
FROM clinics_requests;

-- Create the table for prescriptions
CREATE TABLE IF NOT EXISTS clinics_prescriptions (
    id INTEGER PRIMARY KEY REFERENCES clinics_visits(id) ON DELETE CASCADE,
    clinic_id INTEGER REFERENCES clinics(id),
    prescriptions JSONB DEFAULT '[]',
    created_on timestamptz DEFAULT current_timestamp,
    updated_on timestamptz DEFAULT current_timestamp
);

-- Create safeview for search
CREATE VIEW clinics_prescriptions_safe AS
SELECT *
FROM clinics_prescriptions;


-- Create the table user_sessions
CREATE TABLE IF NOT EXISTS users_sessions (
    id INT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    last_login TIMESTAMPTZ,
    last_logout TIMESTAMPTZ
);

-- Create the table user_sessions
CREATE TABLE IF NOT EXISTS providers_sessions (
    id INT PRIMARY KEY REFERENCES providers(id) ON DELETE CASCADE,
    last_login TIMESTAMPTZ,
    last_logout TIMESTAMPTZ
);


-- Optional: Create a trigger to update `updated_at` on every update
CREATE OR REPLACE FUNCTION update_updated_on_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_on = current_timestamp;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER patients_table_updated_on
BEFORE UPDATE ON patients
FOR EACH ROW
EXECUTE FUNCTION update_updated_on_column();


ALTER DATABASE postgres SET datestyle = 'iso, dmy';
