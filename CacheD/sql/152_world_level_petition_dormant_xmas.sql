-- Current Cached.exe extended slots 361-370: petition external-notify,
-- dormant-gift and Xmas-wish persistence. UpdateMaxLevel and
-- ReloadAccountHighestExpAndLevel are already supplied by migrations 149
-- and 019 respectively.
--
-- The current PE fixes every callable ABI below. Classic 3.0, modified 287
-- and Ertheia provide identical relational bodies for all procedures except
-- lin_GetDormantGiftInfo: the current 2018 (and later 2020) CacheD handler
-- adds char_id to the older two-parameter ABI. The added condition completes
-- the exact (account_id, char_id, type) primary key also used by the paired
-- lin_SetDormantGiftTime write path.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savepetitionexternalnotify"(
    p_world_id integer,
    p_char_name text,
    p_msg_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."petiton_external_notify" AS notification (
        "world_id",
        "char_name",
        "msg_id"
    ) VALUES (
        p_world_id,
        p_char_name,
        p_msg_id
    )
    ON CONFLICT ("world_id", "char_name") DO UPDATE
    SET "msg_id" = EXCLUDED."msg_id"
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepetitionexternalnotify"(
    p_world_id integer,
    p_char_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."petiton_external_notify" AS notification
    WHERE notification."world_id" = p_world_id
      AND "world"."mssql_text_eq"(notification."char_name", p_char_name)
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpetitionexternalnotify"(
    p_world_id integer,
    p_char_name text
) RETURNS TABLE("msg_id" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT notification."msg_id"
    FROM "world"."petiton_external_notify" AS notification
    WHERE notification."world_id" = p_world_id
      AND notification."char_name" LIKE p_char_name
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetdormanthistory"(
    p_account_id integer,
    p_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_dormant" AS dormant
    WHERE dormant."account_id" = p_account_id
      AND dormant."type" = p_type
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_registxmaswish"(
    p_world_id integer,
    p_char_id integer,
    p_char_name text,
    p_wish text
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."xmas_wish" AS wish
        WHERE wish."char_id" = p_char_id
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."xmas_wish" (
        "world_id",
        "char_id",
        "char_name",
        "wish"
    ) VALUES (
        p_world_id,
        p_char_id,
        p_char_name,
        p_wish
    );
    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_confirmxmaswish"(
    p_char_id integer
) RETURNS TABLE("char_id" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT wish."char_id"
    FROM "world"."xmas_wish" AS wish
    WHERE wish."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getdormantgiftinfo"(
    p_account_id integer,
    p_char_id integer,
    p_type integer
) RETURNS TABLE(
    "char_id" integer,
    "gifttime" integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    RETURN QUERY
        SELECT dormant."char_id", dormant."gifttime"
        FROM "world"."user_dormant" AS dormant
        WHERE dormant."account_id" = p_account_id
          AND dormant."char_id" = p_char_id
          AND dormant."type" = p_type;

    IF NOT FOUND THEN
        RETURN QUERY SELECT 0, 0;
    END IF;
END;
$function$;

COMMIT;
