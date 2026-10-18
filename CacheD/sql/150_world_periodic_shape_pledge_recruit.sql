-- Current Cached.exe extended slots 341-350: periodic shape-shift load and
-- the current pledge-recruitment board ABI.
--
-- Cached.exe fixes every parameter/result sequence. Three compatible SQL
-- families agree on the older relational bodies. The current paired
-- L2Server builder vocabulary fixes the two added board fields as
-- recruit_type and pledge_type. Their positions are current-machine-derived.

BEGIN;

ALTER TABLE "world"."pledge_recruit_board"
    ADD COLUMN IF NOT EXISTS "recruit_type" integer NOT NULL DEFAULT 0;

ALTER TABLE "world"."pledge_recruit_board"
    ADD COLUMN IF NOT EXISTS "pledge_type" integer NOT NULL DEFAULT 0;

CREATE OR REPLACE FUNCTION "world"."lin_loadperiodicshapeshifteditems"(
    p_char_id integer
) RETURNS TABLE(
    "item_id" integer,
    "start_time" integer,
    "period" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT appearance."item_id",
           appearance."start_time",
           appearance."period"
    FROM "world"."user_item_apperance" AS appearance
    JOIN "world"."user_item" AS item
      ON item."item_id" = appearance."item_id"
    WHERE item."char_id" = p_char_id
      AND appearance."item_id" > 0
      AND appearance."period" > -1
      AND item."warehouse" < 1000
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_writepledgerecruitboard"(
    p_world_id integer,
    p_pledge_id integer,
    p_registrant_id integer,
    p_pledge_name text,
    p_pledge_master_name text,
    p_pledge_level integer,
    p_pledge_namevalue integer,
    p_trend integer,
    p_register_date integer,
    p_title_message text,
    p_body_message text,
    p_recruit_type integer,
    p_pledge_type integer
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The compatible body treats pledge_id as globally unique,
    -- even though the physical key is world-qualified.
    IF EXISTS (
        SELECT 1
        FROM "world"."pledge_recruit_board" AS board
        WHERE board."pledge_id" = p_pledge_id
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."pledge_recruit_board" (
        "world_id",
        "registrant_id",
        "pledge_id",
        "pledge_name",
        "pledge_master_name",
        "pledge_level",
        "pledge_namevalue",
        "trend",
        "register_date",
        "title_message",
        "body_message",
        "recruit_type",
        "pledge_type"
    ) VALUES (
        p_world_id,
        p_registrant_id,
        p_pledge_id,
        p_pledge_name,
        p_pledge_master_name,
        p_pledge_level,
        p_pledge_namevalue,
        p_trend,
        p_register_date,
        p_title_message,
        p_body_message,
        p_recruit_type,
        p_pledge_type
    );

    RETURN QUERY SELECT 1;
EXCEPTION
    WHEN unique_violation THEN
        RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifypledgerecruitboard"(
    p_world_id integer,
    p_pledge_id integer,
    p_registrant_id integer,
    p_trend integer,
    p_register_date integer,
    p_title_message text,
    p_body_message text,
    p_recruit_type integer,
    p_pledge_type integer
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."pledge_recruit_board" AS board
        WHERE board."pledge_id" = p_pledge_id
    ) OR (
        SELECT count(*)
        FROM "world"."pledge_recruit_board" AS board
        WHERE board."world_id" = p_world_id
          AND board."pledge_id" = p_pledge_id
    ) <> 1 THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."pledge_recruit_board" AS board
    SET "registrant_id" = p_registrant_id,
        "trend" = p_trend,
        "register_date" = p_register_date,
        "title_message" = p_title_message,
        "body_message" = p_body_message,
        "recruit_type" = p_recruit_type,
        "pledge_type" = p_pledge_type
    WHERE board."world_id" = p_world_id
      AND board."pledge_id" = p_pledge_id;

    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_deletepledgewaitingusersbypledgeid"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE("char_id" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
        SELECT waiting."char_id"
        FROM "world"."pledge_recruit_waiting_list" AS waiting
        WHERE waiting."world_id" = p_world_id
          AND waiting."pledge_id" = p_pledge_id;

    DELETE FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."world_id" = p_world_id
      AND waiting."pledge_id" = p_pledge_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_countsearchpledgerecruitboard"(
    p_pledge_name text,
    p_pledge_level integer,
    p_trend integer,
    p_pledge_master_name text,
    p_world_id integer
) RETURNS TABLE("count" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."pledge_recruit_board" AS board
    WHERE board."world_id" = p_world_id
      AND ("world"."mssql_text_eq"(p_pledge_name, '') OR
           lower(board."pledge_name") LIKE
               '%' || lower(p_pledge_name) || '%')
      AND (p_pledge_level = -1 OR
           board."pledge_level" = p_pledge_level)
      AND (p_trend = -1 OR board."trend" = p_trend)
      AND ("world"."mssql_text_eq"(p_pledge_master_name, '') OR
           lower(board."pledge_master_name") LIKE
               '%' || lower(p_pledge_master_name) || '%')
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_searchpledgerecruitboard"(
    p_page_row integer,
    p_pledge_name text,
    p_pledge_level integer,
    p_trend integer,
    p_pledge_master_name text,
    p_page integer,
    p_world_id integer,
    p_sort_condition integer,
    p_order_condition integer,
    p_recruit_type integer
) RETURNS TABLE(
    "pledge_id" integer,
    "pledge_name" text,
    "pledge_master_name" text,
    "pledge_level" integer,
    "pledge_namevalue" integer,
    "trend" integer,
    "title_message" text,
    "recruit_type" integer,
    "pledge_type" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT board."pledge_id",
           board."pledge_name"::text,
           board."pledge_master_name"::text,
           board."pledge_level",
           board."pledge_namevalue",
           board."trend",
           board."title_message"::text,
           board."recruit_type",
           board."pledge_type"
    FROM "world"."pledge_recruit_board" AS board
    WHERE board."world_id" = p_world_id
      AND ("world"."mssql_text_eq"(p_pledge_name, '') OR
           lower(board."pledge_name") LIKE
               '%' || lower(p_pledge_name) || '%')
      AND (p_pledge_level = -1 OR
           board."pledge_level" = p_pledge_level)
      AND (p_trend = -1 OR board."trend" = p_trend)
      AND ("world"."mssql_text_eq"(p_pledge_master_name, '') OR
           lower(board."pledge_master_name") LIKE
               '%' || lower(p_pledge_master_name) || '%')
      AND (p_recruit_type = -1 OR
           board."recruit_type" = p_recruit_type)
    -- Portable policy: these character directions use ICU ko-u-ks-level2.
    -- ICU is the closest cross-platform replace, but its ordering is not
    -- an exact reproduction of legacy SQL Server Wansung-v80 weights.
    ORDER BY
        CASE WHEN p_sort_condition = 1 AND p_order_condition = 1
             THEN (board."pledge_name"::text COLLATE
                      "world"."Korean_Wansung_CI_AS") END ASC,
        CASE WHEN p_sort_condition = 1 AND p_order_condition >= 2
             THEN (board."pledge_name"::text COLLATE
                      "world"."Korean_Wansung_CI_AS") END DESC,
        CASE WHEN p_sort_condition = 2 AND p_order_condition = 1
             THEN (board."pledge_master_name"::text COLLATE
                      "world"."Korean_Wansung_CI_AS") END ASC,
        CASE WHEN p_sort_condition = 2 AND p_order_condition >= 2
             THEN (board."pledge_master_name"::text COLLATE
                      "world"."Korean_Wansung_CI_AS") END DESC,
        CASE WHEN p_sort_condition = 3 AND p_order_condition = 1
             THEN board."pledge_level" END ASC,
        CASE WHEN p_sort_condition = 3 AND p_order_condition >= 2
             THEN board."pledge_level" END DESC,
        CASE WHEN p_sort_condition = 5 AND p_order_condition = 1
             THEN board."trend" END ASC,
        CASE WHEN p_sort_condition = 5 AND p_order_condition >= 2
             THEN board."trend" END DESC,
        board."pledge_namevalue" DESC
    OFFSET p_page_row * ((CASE WHEN p_page = 0 THEN 1 ELSE p_page END) - 1)
    LIMIT p_page_row
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifypledgeinfoonboard"(
    p_world_id integer,
    p_pledge_id integer,
    p_pledge_level integer,
    p_pledge_name text,
    p_pledge_master_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_recruit_board" AS board
    SET "pledge_level" = p_pledge_level,
        "pledge_name" = p_pledge_name,
        "pledge_master_name" = p_pledge_master_name
    WHERE board."pledge_id" = p_pledge_id
      AND board."world_id" = p_world_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeonpledgerecruitboard"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE(
    "trend" integer,
    "title_message" text,
    "body_message" text,
    "recruit_type" integer,
    "pledge_type" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT board."trend",
           board."title_message"::text,
           board."body_message"::text,
           board."recruit_type",
           board."pledge_type"
    FROM "world"."pledge_recruit_board" AS board
    WHERE board."world_id" = p_world_id
      AND board."pledge_id" = p_pledge_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getuseronpledgewaitinglist2"(
    p_world_id integer,
    p_character_id integer
) RETURNS TABLE(
    "pledge_id" integer,
    "pledge_name" text,
    "pledge_master_name" text,
    "pledge_level" integer,
    "trend" integer,
    "title_message" text,
    "self_intro" text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT waiting."pledge_id",
           board."pledge_name"::text,
           board."pledge_master_name"::text,
           board."pledge_level",
           board."trend",
           board."title_message"::text,
           waiting."self_intro"::text
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    JOIN "world"."pledge_recruit_board" AS board
      ON board."pledge_id" = waiting."pledge_id"
    WHERE waiting."world_id" = p_world_id
      AND waiting."char_id" = p_character_id
$function$;

COMMIT;
