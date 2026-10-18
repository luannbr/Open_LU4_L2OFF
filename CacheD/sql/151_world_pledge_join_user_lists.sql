-- Current Cached.exe extended slots 351-360: pledge waiting/draft user-list
-- mutations and searches.
--
-- The current PE fixes all callable parameter/result shapes. Compatible SQL
-- families supply the relational bodies. lin_WritePledgeWaitingUser is the
-- one versioned exception: current Cached.exe binds and fetches an integer
-- result, whereas the available older donors expose only their INSERT body.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_writepledgewaitinguser"(
    p_world_id integer,
    p_char_id integer,
    p_name text,
    p_class integer,
    p_level integer,
    p_pledge_id integer,
    p_register_date double precision,
    p_self_intro text
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."pledge_recruit_waiting_list" (
        "world_id",
        "char_id",
        "name",
        "class",
        "level",
        "pledge_id",
        "register_date",
        "self_intro"
    ) VALUES (
        p_world_id,
        p_char_id,
        p_name,
        p_class,
        p_level,
        p_pledge_id,
        p_register_date,
        p_self_intro
    );
    RETURN QUERY SELECT 1;
EXCEPTION
    WHEN unique_violation THEN
        RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getuseronpledgewaitinglist"(
    p_world_id integer,
    p_target_id integer,
    p_pledge_id integer
) RETURNS TABLE("self_intro" text)
LANGUAGE sql
STABLE
AS $function$
    SELECT waiting."self_intro"::text
    FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting."world_id" = p_world_id
      AND waiting."pledge_id" = p_pledge_id
      AND waiting."char_id" = p_target_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_writepledgedraftuser"(
    p_world_id integer,
    p_char_id integer,
    p_name text,
    p_class integer,
    p_level integer,
    p_trend integer,
    p_register_date integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_draft_user_list" (
        "world_id",
        "char_id",
        "name",
        "class",
        "level",
        "trend",
        "register_date"
    ) VALUES (
        p_world_id,
        p_char_id,
        p_name,
        p_class,
        p_level,
        p_trend,
        p_register_date
    )
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_searchpledgedraftuserlist"(
    p_world_id integer,
    p_min_level integer,
    p_max_level integer,
    p_class_list text,
    p_name text,
    p_sort_condition integer,
    p_order_condition integer
) RETURNS TABLE(
    "char_id" integer,
    "name" text,
    "class" integer,
    "level" integer,
    "trend" integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    -- All compatible SQL bodies accept world_id but omit it from the SELECT.
    -- Preserve that production-visible cross-world behavior until a closer
    -- current database source proves that the procedure changed.
    PERFORM p_world_id;

    -- The donor builds ORDER BY dynamically. An order without one of its four
    -- recognized sort columns produces invalid SQL and therefore no CacheD
    -- reply. Raise here to keep the same ExecutePrepared failure boundary.
    IF (p_sort_condition <> 0 OR p_order_condition <> 0)
       AND p_sort_condition NOT IN (1, 2, 3, 4) THEN
        RAISE EXCEPTION
            'invalid lin_SearchPledgeDraftUserList sort condition: %',
            p_sort_condition;
    END IF;

    IF p_sort_condition = 1 THEN
        -- Portable policy: name ordering uses ICU ko-u-ks-level2 and therefore
        -- approximates, instead of exactly reproduces, legacy
        -- SQL Server Wansung-v80 weights. NULL placement remains exact.
        IF p_order_condition = 2 THEN
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY
                    (draft."name"::text COLLATE
                        "world"."Korean_Wansung_CI_AS") DESC NULLS LAST,
                    draft."register_date" DESC;
        ELSE
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY
                    (draft."name"::text COLLATE
                        "world"."Korean_Wansung_CI_AS") ASC NULLS FIRST,
                    draft."register_date" DESC;
        END IF;
    ELSIF p_sort_condition = 2 THEN
        IF p_order_condition = 2 THEN
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."trend" DESC, draft."register_date" DESC;
        ELSE
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."trend" ASC, draft."register_date" DESC;
        END IF;
    ELSIF p_sort_condition = 3 THEN
        IF p_order_condition = 2 THEN
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."class" DESC, draft."register_date" DESC;
        ELSE
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."class" ASC, draft."register_date" DESC;
        END IF;
    ELSIF p_sort_condition = 4 THEN
        IF p_order_condition = 2 THEN
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."level" DESC, draft."register_date" DESC;
        ELSE
            RETURN QUERY
                SELECT draft."char_id", draft."name"::text,
                       draft."class", draft."level", draft."trend"
                FROM "world"."pledge_draft_user_list" AS draft
                WHERE draft."level" >= p_min_level
                  AND draft."level" <= p_max_level
                  AND ("world"."mssql_text_eq"(p_name, '') OR
                       lower(draft."name") LIKE
                           '%' || lower(p_name) || '%')
                  AND ("world"."mssql_text_eq"(p_class_list, '') OR
                       draft."class" = ANY(
                      string_to_array(p_class_list, ',')::integer[]))
                ORDER BY draft."level" ASC, draft."register_date" DESC;
        END IF;
    ELSE
        RETURN QUERY
            SELECT draft."char_id", draft."name"::text,
                   draft."class", draft."level", draft."trend"
            FROM "world"."pledge_draft_user_list" AS draft
            WHERE draft."level" >= p_min_level
              AND draft."level" <= p_max_level
              AND ("world"."mssql_text_eq"(p_name, '') OR
                   lower(draft."name") LIKE
                       '%' || lower(p_name) || '%')
              AND ("world"."mssql_text_eq"(p_class_list, '') OR
                   draft."class" = ANY(
                  string_to_array(p_class_list, ',')::integer[]))
            ORDER BY draft."register_date" DESC;
    END IF;
END;
$function$;

COMMIT;
