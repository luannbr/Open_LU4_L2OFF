-- Extended requests 201-210. Current Cached.exe fixes the callable names,
-- parameter SQL types and result order. Compatible SQL donors fix the row
-- effects. In particular, CUser::SaveQuestNPCLog has two current call paths:
-- its ordinary count is SQL_INTEGER while the bulk path binds the same Win32
-- int value as SQL_DOUBLE, so both PostgreSQL overloads are required.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updateusernpclog"(
    p_char_id integer,
    p_quest_no integer,
    p_npc_id integer,
    p_type integer,
    p_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated integer;
BEGIN
    UPDATE "world"."user_npc_log" AS npc_log
    SET "count" = p_count
    WHERE npc_log."char_id" = p_char_id
      AND npc_log."quest_no" = p_quest_no
      AND npc_log."npc_id" = p_npc_id
      AND npc_log."type" = p_type;

    GET DIAGNOSTICS v_updated = ROW_COUNT;
    IF v_updated = 0 THEN
        INSERT INTO "world"."user_npc_log" (
            "char_id", "quest_no", "npc_id", "type", "count"
        ) VALUES (
            p_char_id, p_quest_no, p_npc_id, p_type::smallint, p_count
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateusernpclog"(
    p_char_id integer,
    p_quest_no integer,
    p_npc_id integer,
    p_type integer,
    p_count double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    PERFORM "world"."lin_updateusernpclog"(
        p_char_id,
        p_quest_no,
        p_npc_id,
        p_type,
        p_count::integer);
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getinzoneflag"(
    p_inzone_uid integer,
    p_inzone_type_id integer
) RETURNS TABLE (
    flag_no integer,
    flag_value integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT flag."flag_no", flag."flag_value"
    FROM "world"."instantzone_flag" AS flag
    WHERE flag."instantzone_uid" = p_inzone_uid
      AND EXISTS (
          SELECT 1
          FROM "world"."instantzone" AS inzone
          WHERE inzone."instantzone_uid" = p_inzone_uid
            AND (
                p_inzone_type_id = 0
                OR inzone."instantzone_type_id" = p_inzone_type_id
            )
      );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createinstantzone"(
    p_inzone_type_id integer,
    p_inzone_db_id integer,
    p_create_date integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."instantzone" (
        "instantzone_uid", "instantzone_type_id", "create_date"
    ) VALUES (
        p_inzone_db_id, p_inzone_type_id, p_create_date
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveinzoneflag"(
    p_inzone_uid integer,
    p_flag_no integer,
    p_flag_value integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated integer;
BEGIN
    UPDATE "world"."instantzone_flag" AS flag
    SET "flag_value" = p_flag_value
    WHERE flag."instantzone_uid" = p_inzone_uid
      AND flag."flag_no" = p_flag_no;

    GET DIAGNOSTICS v_updated = ROW_COUNT;
    IF v_updated = 0 THEN
        INSERT INTO "world"."instantzone_flag" (
            "instantzone_uid", "flag_no", "flag_value"
        ) VALUES (
            p_inzone_uid, p_flag_no, p_flag_value
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinzonemaxdbid"()
RETURNS TABLE (
    max_db_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(MAX(inzone."instantzone_uid"), 0)::integer
    FROM "world"."instantzone" AS inzone;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_cleareventtime"(
    p_account_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_event_time" AS event_time
    WHERE event_time."account_id" = p_account_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpostadditionalfriend"(
    p_char_id integer,
    p_friend_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."post_additional_friend" (
        "char_id", "friend_name"
    ) VALUES (
        p_char_id, p_friend_name
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostadditionalfriend"(
    p_char_id integer
) RETURNS TABLE (
    friend_name text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT friend."friend_name"::text
    FROM "world"."post_additional_friend" AS friend
    WHERE friend."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepostadditionalfriend"(
    p_char_id integer,
    p_friend_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."post_additional_friend" AS friend
    WHERE friend."char_id" = p_char_id
      AND "world"."mssql_text_eq"(friend."friend_name", p_friend_name);
$function$;

COMMIT;
