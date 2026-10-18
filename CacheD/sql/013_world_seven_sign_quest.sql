-- Current Cached.exe SevenSignQuestDB persistence ABI.
--
-- The six call signatures, result order and all bind widths come from the
-- current 0x00596888..0x00597863 machine block.  Classic 3.0, Ertheia and
-- protocol-287 donors agree on the relations and statement effects.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadssqsysteminfo"()
RETURNS TABLE (
    round_number integer,
    status smallint,
    winner smallint,
    event_start_time integer,
    event_end_time integer,
    seal_effect_time integer,
    seal_effect_end_time integer,
    seal1 smallint,
    seal2 smallint,
    seal3 smallint,
    seal4 smallint,
    seal5 smallint,
    seal6 smallint,
    seal7 smallint,
    castle_snapshot_time integer,
    can_drop_guard integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT data.round_number,
           data.status,
           data.winner,
           data.event_start_time,
           data.event_end_time,
           data.seal_effect_time,
           data.seal_effect_end_time,
           data.seal1,
           data.seal2,
           data.seal3,
           data.seal4,
           data.seal5,
           data.seal6,
           data.seal7,
           COALESCE(data.castle_snapshot_time, 0),
           COALESCE(data.can_drop_guard, 0)
    FROM "world"."ssq_data" AS data
    ORDER BY data.round_number DESC
    LIMIT 2;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadssqjoininfo"(
    p_round_number integer
) RETURNS TABLE (
    point bigint,
    collected_point bigint,
    main_event_point bigint,
    type integer,
    member_count integer,
    seal1_selection_count smallint,
    seal2_selection_count smallint,
    seal3_selection_count smallint,
    seal4_selection_count smallint,
    seal5_selection_count smallint,
    seal6_selection_count smallint,
    seal7_selection_count smallint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT data.point,
           data.collected_point,
           data.main_event_point,
           data.type::integer,
           data.member_count,
           data.seal1_selection_count::smallint,
           data.seal2_selection_count::smallint,
           data.seal3_selection_count::smallint,
           data.seal4_selection_count::smallint,
           data.seal5_selection_count::smallint,
           data.seal6_selection_count::smallint,
           data.seal7_selection_count::smallint
    FROM "world"."ssq_join_data" AS data
    WHERE data.round_number = p_round_number;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadssqwinner"(
    p_round_number integer
) RETURNS TABLE (winner smallint)
LANGUAGE sql
STABLE
AS $function$
    SELECT data.winner
    FROM "world"."ssq_data" AS data
    WHERE data.round_number = p_round_number;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createssqround"(
    p_round_number integer,
    p_status integer,
    p_winner integer,
    p_event_start_time integer,
    p_event_end_time integer,
    p_seal_effect_time integer,
    p_seal_effect_end_time integer,
    p_seal1 integer,
    p_seal2 integer,
    p_seal3 integer,
    p_seal4 integer,
    p_seal5 integer,
    p_seal6 integer,
    p_seal7 integer,
    p_castle_snapshot_time integer,
    p_can_drop_guard integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."ssq_join_data" (
        round_number,
        point,
        collected_point,
        main_event_point,
        type,
        member_count,
        seal1_selection_count,
        seal2_selection_count,
        seal3_selection_count,
        seal4_selection_count,
        seal5_selection_count,
        seal6_selection_count,
        seal7_selection_count,
        last_changed_time
    ) VALUES
        (p_round_number, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
         "world"."mssql_getdate"()),
        (p_round_number, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
         "world"."mssql_getdate"());

    INSERT INTO "world"."ssq_data" (
        round_number,
        status,
        winner,
        event_start_time,
        event_end_time,
        seal_effect_time,
        seal_effect_end_time,
        seal1,
        seal2,
        seal3,
        seal4,
        seal5,
        seal6,
        seal7,
        last_changed_time,
        castle_snapshot_time,
        can_drop_guard
    ) VALUES (
        p_round_number,
        p_status,
        p_winner,
        p_event_start_time,
        p_event_end_time,
        p_seal_effect_time,
        p_seal_effect_end_time,
        p_seal1,
        p_seal2,
        p_seal3,
        p_seal4,
        p_seal5,
        p_seal6,
        p_seal7,
        "world"."mssql_getdate"(),
        p_castle_snapshot_time,
        p_can_drop_guard
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savessqsysteminfo"(
    p_round_number integer,
    p_status integer,
    p_winner integer,
    p_event_start_time integer,
    p_event_end_time integer,
    p_seal_effect_time integer,
    p_seal_effect_end_time integer,
    p_seal1 integer,
    p_seal2 integer,
    p_seal3 integer,
    p_seal4 integer,
    p_seal5 integer,
    p_seal6 integer,
    p_seal7 integer,
    p_castle_snapshot_time integer,
    p_can_drop_guard boolean
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."ssq_data" AS data
    SET status = p_status,
        winner = p_winner,
        event_start_time = p_event_start_time,
        event_end_time = p_event_end_time,
        seal_effect_time = p_seal_effect_time,
        seal_effect_end_time = p_seal_effect_end_time,
        seal1 = p_seal1,
        seal2 = p_seal2,
        seal3 = p_seal3,
        seal4 = p_seal4,
        seal5 = p_seal5,
        seal6 = p_seal6,
        seal7 = p_seal7,
        castle_snapshot_time = p_castle_snapshot_time,
        can_drop_guard = CASE WHEN p_can_drop_guard THEN 1 ELSE 0 END,
        last_changed_time = "world"."mssql_getdate"()
    WHERE data.round_number = p_round_number;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savessqjoininfo"(
    p_round_number integer,
    p_type integer,
    p_point bigint,
    p_collected_point bigint,
    p_main_event_point bigint,
    p_member_count integer,
    p_seal1 integer,
    p_seal2 integer,
    p_seal3 integer,
    p_seal4 integer,
    p_seal5 integer,
    p_seal6 integer,
    p_seal7 integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."ssq_join_data" AS data
    SET point = p_point,
        collected_point = p_collected_point,
        main_event_point = p_main_event_point,
        member_count = p_member_count,
        seal1_selection_count = p_seal1,
        seal2_selection_count = p_seal2,
        seal3_selection_count = p_seal3,
        seal4_selection_count = p_seal4,
        seal5_selection_count = p_seal5,
        seal6_selection_count = p_seal6,
        seal7_selection_count = p_seal7,
        last_changed_time = "world"."mssql_getdate"()
    WHERE data.round_number = p_round_number
      AND data.type = p_type;
$function$;

COMMIT;
