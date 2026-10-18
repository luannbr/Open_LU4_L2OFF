-- Current Cached.exe extended requests 311 through 320.
--
-- The executable fixes all ten wire contracts and exact SQL call ABIs.
-- Classic 3.0, modified 287 and Ertheia keep matching relational bodies.
-- All four reached relations already belong to the selected stable baseline.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadkalieevent"(
    p_event_id integer
) RETURNS TABLE (
    currentstate integer,
    progress integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."event_kalie" AS event
        WHERE event."event_id" = p_event_id
    ) THEN
        INSERT INTO "world"."event_kalie" (
            "event_id",
            "currentstate",
            "progress"
        ) VALUES (
            p_event_id,
            1,
            0
        );
    END IF;

    RETURN QUERY
    SELECT
        event."currentstate",
        event."progress"
    FROM "world"."event_kalie" AS event
    WHERE event."event_id" = p_event_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadkalieeventuser"(
    p_char_id integer
) RETURNS TABLE (
    remaintoken integer
)
LANGUAGE sql
STABLE
AS $function$
    -- The nested scalar query makes the donor return one row containing -1
    -- when no per-user record exists.
    SELECT COALESCE((
        SELECT event_user."remaintoken"
        FROM "world"."user_event_kalie" AS event_user
        WHERE event_user."char_id" = p_char_id
    ), -1);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatekalieevent"(
    p_event_id integer,
    p_currentstate integer,
    p_progress integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."event_kalie" AS event
        WHERE event."event_id" = p_event_id
    ) THEN
        UPDATE "world"."event_kalie" AS event
        SET "currentstate" = p_currentstate,
            "progress" = p_progress
        WHERE event."event_id" = p_event_id;
    ELSE
        INSERT INTO "world"."event_kalie" (
            "event_id",
            "currentstate",
            "progress"
        ) VALUES (
            p_event_id,
            p_currentstate,
            p_progress
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatekalieeventuser"(
    p_char_id integer,
    p_token integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_event_kalie" AS event_user
        WHERE event_user."char_id" = p_char_id
    ) THEN
        UPDATE "world"."user_event_kalie" AS event_user
        SET "remaintoken" = p_token
        WHERE event_user."char_id" = p_char_id;
    ELSE
        INSERT INTO "world"."user_event_kalie" (
            "char_id",
            "remaintoken"
        ) VALUES (
            p_char_id,
            p_token
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcompeteagitstatus"(
    p_agit_id integer,
    p_elapsed_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."agit_compete_status" AS status
        WHERE status."agit_id" = p_agit_id
    ) THEN
        INSERT INTO "world"."agit_compete_status" (
            "agit_id",
            "elapsed_time",
            "winner_pledge_id"
        ) VALUES (
            p_agit_id,
            p_elapsed_time,
            0
        );
    ELSE
        UPDATE "world"."agit_compete_status" AS status
        SET "elapsed_time" = p_elapsed_time
        WHERE status."agit_id" = p_agit_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcompeteagitwinner"(
    p_agit_id integer,
    p_winner_pledge_id integer,
    p_accept_owner integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit_compete_status" AS status
    SET "winner_pledge_id" = p_winner_pledge_id,
        "accept_owner" = p_accept_owner
    WHERE status."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetcompeteagitstatus"(
    p_agit_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."agit_compete_status" AS status
    WHERE status."agit_id" = p_agit_id;

    UPDATE "world"."agit" AS agit
    SET "previous_owner_pledge" = 0
    WHERE agit."id" = p_agit_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadcompeteagitstatus"(
    p_agit_id integer
) RETURNS TABLE (
    elapsed_time integer,
    winner_pledge_id integer,
    previous_owner_pledge integer,
    accept_owner integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        status."elapsed_time",
        status."winner_pledge_id",
        COALESCE(agit."previous_owner_pledge", 0),
        status."accept_owner"
    FROM "world"."agit_compete_status" AS status
    INNER JOIN "world"."agit" AS agit
        ON status."agit_id" = agit."id"
    WHERE status."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadexdropcount"(
    p_char_id integer
) RETURNS TABLE (
    char_id integer,
    exdrop_id integer,
    get_count integer,
    get_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        info."char_id",
        info."exdrop_id",
        info."get_count",
        info."get_time"
    FROM "world"."ex_drop_info" AS info
    WHERE info."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateexdropcount"(
    p_char_id integer,
    p_exdrop_id integer,
    p_get_count integer,
    p_get_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."ex_drop_info" AS info
        WHERE info."char_id" = p_char_id
          AND info."exdrop_id" = p_exdrop_id
    ) THEN
        -- Preserve the donor body: an existing row updates get_count only.
        -- the newly supplied get_time is ignored.
        UPDATE "world"."ex_drop_info" AS info
        SET "get_count" = p_get_count
        WHERE info."char_id" = p_char_id
          AND info."exdrop_id" = p_exdrop_id;
    ELSE
        INSERT INTO "world"."ex_drop_info" (
            "char_id",
            "exdrop_id",
            "get_count",
            "get_time"
        ) VALUES (
            p_char_id,
            p_exdrop_id,
            p_get_count,
            p_get_time
        );
    END IF;
END;
$function$;

COMMIT;
