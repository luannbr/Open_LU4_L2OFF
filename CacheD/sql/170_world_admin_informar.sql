-- Current Cached.exe AQ opcode 179.
--
-- The image fixes the zero-input delete function, the one-UTF-16-input set
-- function and their void results. No supplied SQL family keeps either
-- procedure body or its physical table. The singleton relation below is an
-- explicit portable persistence model for that observable ABI, not a claim
-- about the unavailable MSSQL table name or implementation.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."cached_portable_informar" (
    "singleton_id" integer NOT NULL,
    "content" text NOT NULL,
    CONSTRAINT "pk_cached_portable_informar"
        PRIMARY KEY ("singleton_id"),
    CONSTRAINT "ck_cached_portable_informar_singleton"
        CHECK ("singleton_id" = 1)
);

CREATE OR REPLACE FUNCTION "world"."lin_setinformar"(
    p_content text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_informar" AS informar (
        "singleton_id",
        "content"
    ) VALUES (
        1,
        COALESCE(p_content, '')
    )
    ON CONFLICT ("singleton_id") DO UPDATE
       SET "content" = EXCLUDED."content";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteinformar"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_informar";
$function$;

COMMIT;
