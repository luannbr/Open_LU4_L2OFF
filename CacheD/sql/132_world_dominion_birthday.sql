-- Extended requests 161-170. Current Cached.exe fixes every packet, callable
-- and result-column ABI. Classic 3.0 and the compatible 287/Ertheia SQL
-- families agree on the relational bodies below. The update-then-insert
-- functions keep the donor tables' no-key behavior rather
-- than adding portable-only uniqueness constraints.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loaddominion"(
    p_dominion_id integer
) RETURNS TABLE (
    dominion_status integer,
    residence_status integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT dominion."dominion_status", dominion."residence_status"
    FROM "world"."dominion" AS dominion
    WHERE dominion."dominion_id" = p_dominion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedominion"(
    p_dominion_id integer,
    p_dominion_status integer,
    p_residence_status integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."dominion" AS dominion
    SET "dominion_status" = p_dominion_status,
        "residence_status" = p_residence_status
    WHERE dominion."dominion_id" = p_dominion_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."dominion" (
            "dominion_id",
            "dominion_status",
            "residence_status"
        ) VALUES (
            p_dominion_id,
            p_dominion_status,
            p_residence_status
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedominionregistry"(
    p_dominion_id integer,
    p_registry_type integer,
    p_registry_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."dominion_registry" (
        "dominion_id",
        "registry_type",
        "registry_id"
    ) VALUES (
        p_dominion_id,
        p_registry_type,
        p_registry_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetdominionregistry"(
    p_dominion_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."dominion_registry" AS registry
    WHERE registry."dominion_id" = p_dominion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletedominionregistry"(
    p_dominion_id integer,
    p_registry_type integer,
    p_registry_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."dominion_registry" AS registry
    WHERE registry."dominion_id" = p_dominion_id
      AND registry."registry_type" = p_registry_type
      AND registry."registry_id" = p_registry_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddominionregistry"(
    p_dominion_id integer
) RETURNS TABLE (
    registry_type integer,
    registry_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT registry."registry_type", registry."registry_id"
    FROM "world"."dominion_registry" AS registry
    WHERE registry."dominion_id" = p_dominion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveownthing"(
    p_original_dominion_id integer,
    p_current_dominion_id integer,
    p_pos_x integer,
    p_pos_y integer,
    p_pos_z integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."ownthing" AS ownthing
    SET "current_dominion_id" = p_current_dominion_id,
        "pos_x" = p_pos_x,
        "pos_y" = p_pos_y,
        "pos_z" = p_pos_z
    WHERE ownthing."original_dominion_id" = p_original_dominion_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."ownthing" (
            "original_dominion_id",
            "current_dominion_id",
            "pos_x",
            "pos_y",
            "pos_z"
        ) VALUES (
            p_original_dominion_id,
            p_current_dominion_id,
            p_pos_x,
            p_pos_y,
            p_pos_z
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadownthing"(
    p_original_dominion_id integer
) RETURNS TABLE (
    current_dominion_id integer,
    pos_x integer,
    pos_y integer,
    pos_z integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        ownthing."current_dominion_id",
        ownthing."pos_x",
        ownthing."pos_y",
        ownthing."pos_z"
    FROM "world"."ownthing" AS ownthing
    WHERE ownthing."original_dominion_id" = p_original_dominion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedominionsiege"(
    p_siege_state integer,
    p_prev_siege_end_time integer,
    p_siege_elapsed_time integer,
    p_next_siege_start_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."dominion_siege" AS siege
    SET "siege_state" = p_siege_state,
        "prev_siege_end_time" = p_prev_siege_end_time,
        "siege_elapsed_time" = p_siege_elapsed_time,
        "next_siege_start_time" = p_next_siege_start_time;

    IF NOT FOUND THEN
        INSERT INTO "world"."dominion_siege" (
            "siege_state",
            "prev_siege_end_time",
            "siege_elapsed_time",
            "next_siege_start_time"
        ) VALUES (
            p_siege_state,
            p_prev_siege_end_time,
            p_siege_elapsed_time,
            p_next_siege_start_time
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddominionsiege"()
RETURNS TABLE (
    siege_state integer,
    prev_siege_end_time integer,
    siege_elapsed_time integer,
    next_siege_start_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        siege."siege_state",
        siege."prev_siege_end_time",
        siege."siege_elapsed_time",
        siege."next_siege_start_time"
    FROM "world"."dominion_siege" AS siege;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savebirthdaygift"(
    p_char_id integer,
    p_get_date text,
    p_event_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_birthday_history" AS birthday
    SET "get_date" = p_get_date::timestamp without time zone,
        "event_id" = p_event_id,
        "gift_index" = COALESCE(birthday."gift_index", 0) + 1
    WHERE birthday."char_id" = p_char_id
      AND birthday."event_id" = p_event_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_birthday_history" (
            "char_id",
            "get_date",
            "gift_index",
            "event_id"
        ) VALUES (
            p_char_id,
            p_get_date::timestamp without time zone,
            1,
            p_event_id
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getbirthdaygiftcount"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (gift_index integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(birthday."gift_index", 0)::integer
    FROM "world"."user_birthday_history" AS birthday
    WHERE birthday."char_id" = p_char_id
      AND birthday."event_id" = p_event_id;
$function$;

COMMIT;
