-- Current Cached.exe TimeAttackDB persistence ABI.
--
-- Bind order and widths come from current machine code at
-- 0x0059be44..0x0059ca99. The Classic 3.0 procedure bodies independently
-- confirm the relation effects and conditions.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createtimeattackrecord"(
    p_room_no integer,
    p_record_type integer,
    p_ssq_round integer,
    p_ssq_part integer,
    p_point bigint,
    p_record_time bigint,
    p_elapsed_time integer,
    p_member_count integer,
    p_member_names text,
    p_member_dbid_list text,
    p_member_reward_flags integer,
    p_fee bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."time_attack_record" (
        room_no,
        record_type,
        ssq_round,
        ssq_part,
        point,
        record_time,
        elapsed_time,
        member_count,
        member_names,
        member_dbid_list,
        member_reward_flags,
        fee
    ) VALUES (
        p_room_no::smallint,
        p_record_type::smallint,
        p_ssq_round,
        p_ssq_part::smallint,
        p_point::integer,
        p_record_time::integer,
        p_elapsed_time,
        p_member_count,
        p_member_names,
        p_member_dbid_list,
        p_member_reward_flags,
        p_fee::integer
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savetimeattackrecord"(
    p_room_no integer,
    p_record_type integer,
    p_ssq_round integer,
    p_ssq_part integer,
    p_point bigint,
    p_record_time bigint,
    p_elapsed_time integer,
    p_member_count integer,
    p_member_names text,
    p_member_dbid_list text,
    p_member_reward_flags integer,
    p_fee bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."time_attack_record" AS record
    SET ssq_part = p_ssq_part::smallint,
        point = p_point::integer,
        record_time = p_record_time::integer,
        elapsed_time = p_elapsed_time,
        member_count = p_member_count,
        member_names = p_member_names,
        member_dbid_list = p_member_dbid_list,
        member_reward_flags = p_member_reward_flags,
        fee = p_fee::integer
    WHERE record.room_no = p_room_no
      AND record.record_type = p_record_type
      AND record.ssq_round = p_ssq_round;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtimeattackrecord"(
    p_ssq_round integer
) RETURNS TABLE (
    room_no integer,
    record_type integer,
    ssq_round integer,
    ssq_part integer,
    point integer,
    record_time integer,
    elapsed_time integer,
    member_count integer,
    member_names text,
    member_dbid_list text,
    member_reward_flags integer,
    fee integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT record.room_no::integer,
           record.record_type::integer,
           record.ssq_round,
           record.ssq_part::integer,
           record.point,
           record.record_time,
           record.elapsed_time,
           record.member_count,
           record.member_names::text,
           COALESCE(record.member_dbid_list, '')::text,
           COALESCE(record.member_reward_flags, 0),
           COALESCE(record.fee, 0)
    FROM "world"."time_attack_record" AS record
    WHERE record.ssq_round = p_ssq_round;
$function$;

COMMIT;
