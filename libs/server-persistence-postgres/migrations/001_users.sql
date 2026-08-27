BEGIN;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version bigint PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS users (
    user_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    normalized_name varchar(32) NOT NULL UNIQUE,
    display_name varchar(32) NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

INSERT INTO schema_migrations(version)
VALUES (1)
ON CONFLICT (version) DO NOTHING;

COMMIT;
